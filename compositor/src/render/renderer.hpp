#pragma once

// Il renderer di Vela (docs/renderer.md §6-7): tutto ciò che si disegna passa
// da qui, sul nostro device Vulkan.
//
// Verso wlroots si presenta come un wlr_renderer. Così anche ciò che wlroots
// disegna per conto suo usa i nostri pixel e il nostro device: il cursore
// hardware, le catture degli schermi (screencopy, ext-image-copy-capture), il
// caricamento dei buffer delle app. Non esiste un secondo renderer.

#include "render/vulkan.hpp"

#include <functional>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace vela::render {

class Pass;
struct Texture;

// Un buffer dmabuf importato come destinazione di disegno (schermo, cursore,
// cattura). Vive quanto il buffer (wlr_addon).
struct RenderTarget {
    class Renderer* owner;
    wlr_addon addon;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat format; // della vista: _SRGB, la GPU codifica scrivendo
    int dmabufFd; // piano 0, per la sincronizzazione implicita (non nostro)
    uint32_t width;
    uint32_t height;
    bool initialized; // il contenuto va conservato tra un disegno e l'altro
    bool sampleable; // si può anche leggere come texture (la sfocatura legge ciò che c'è dietro)
    uint64_t lastUse; // punto della timeline dell'ultimo disegno
};

class Renderer {
public:
    // Il Renderer appartiene a wlroots: si distrugge con
    // wlr_renderer_destroy(renderer->wlr()), che avvisa prima chi lo usa.
    static Renderer* create(VulkanDevice& vk);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    wlr_renderer* wlr() { return &m_shim.base; }
    static Renderer* from(wlr_renderer* renderer);
    VulkanDevice& vk() { return m_vk; }

    // Comincia un disegno su `buffer` (un dmabuf in uno dei renderFormats).
    std::unique_ptr<Pass> beginPass(wlr_buffer* buffer);

    // ------------------------------------------ sincronizzazione esplicita --

    // Una timeline syncobj nostra (linux-drm-syncobj-v1, §7.3): ogni disegno
    // ne fa scattare un punto quando la GPU ha finito. Sono i punti di
    // rilascio dei buffer delle app. nullptr se il kernel non la supporta.
    wlr_drm_syncobj_timeline* syncTimeline() const { return m_syncTimeline; }
    // Fa scattare il prossimo punto della timeline con `syncFile` (non lo
    // chiude; -1: il lavoro è già finito). Restituisce il punto, 0 se non
    // c'è la timeline.
    uint64_t signalSyncPoint(int syncFile);

    // ------------------------------------------------ tempi della GPU --

    // Quando la GPU ha cominciato e finito un disegno (§4.3). Con i
    // timestamp calibrati gli istanti sono su CLOCK_MONOTONIC (absolute);
    // altrimenti conta solo la durata.
    struct GpuTiming {
        int64_t startNs = 0;
        int64_t endNs = 0;
        bool absolute = false;
    };
    // Uno slot di misura per il prossimo command buffer; -1 se non ci sono
    // i timestamp o gli slot sono tutti in uso. Gli slot girano: una misura
    // va letta entro qualche decina di disegni.
    int timingSlot();
    void writeTimestamp(VkCommandBuffer cmd, int slot, bool end);
    void timingSubmitted(int slot, uint64_t point);
    // true se il disegno (slot, punto della timeline) è finito e la misura
    // è ancora lì.
    bool readTiming(int slot, uint64_t point, GpuTiming& out);

    // ------------------------------------------------ per Pass e Texture --

    RenderTarget* targetFor(wlr_buffer* buffer);
    const wlr_drm_format_set* shmFormatSet() const { return &m_shmFormats; }
    void trackTexture(Texture* texture, bool alive);

    // Un command buffer libero, già in registrazione.
    VkCommandBuffer beginCommands();

    // I caricamenti dei buffer wl_shm si registrano qui e partono verso la
    // GPU insieme al prossimo invio, prima di esso.
    VkCommandBuffer uploadCommands();
    struct Staging {
        VkBuffer buffer;
        VkDeviceSize offset;
        uint8_t* data;
    };
    bool stage(VkDeviceSize size, Staging& out);

    // Invia `cmd` (già chiuso). waitSyncFiles: sync_file da aspettare prima
    // (vengono chiusi). releaseFd, se non null, riceve un sync_file che
    // scatta quando il lavoro è finito (-1 se non disponibile: in quel caso
    // la CPU ha già aspettato). Restituisce il punto della timeline, 0 se
    // l'invio è fallito.
    uint64_t submit(VkCommandBuffer cmd, std::vector<int>& waitSyncFiles, int* releaseFd);
    void waitFor(uint64_t point);
    uint64_t completed();
    uint64_t lastSubmitted() const { return m_lastPoint; }

    // Esegue `fn` quando la GPU avrà finito tutto il lavoro inviato fin qui
    // e quello in registrazione (ad esempio distruggere una texture usata
    // dall'ultimo frame).
    void defer(std::function<void()> fn);
    void collect();

    // Pipeline: una per (formato di destinazione, tipo, fusione).
    enum class PipelineKind { Texture, Rect, Shadow, BlurDown, BlurUp, BlurMix };
    VkPipeline pipeline(VkFormat target, PipelineKind kind, bool blend);
    VkPipelineLayout pipelineLayout() const { return m_layout; }
    VkSampler sampler(bool linear) const { return linear ? m_linear : m_nearest; }

    // ------------------------------------------- sfocatura (§8.3) --

    // Le immagini di lavoro della sfocatura: una catena di livelli, ognuno a
    // metà risoluzione del precedente (il primo a metà dello schermo), in
    // virgola mobile a 16 bit: lineare, senza bande. Crescono quando serve
    // e si riusano da un disegno all'altro.
    static constexpr int blurLevels = 4;
    static constexpr VkFormat blurFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    struct BlurImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    // Livelli abbastanza grandi per sfocare una zona di width x height pixel.
    bool prepareBlur(uint32_t width, uint32_t height);
    const BlurImage& blurImage(int level) const { return m_blur[level]; }

private:
    explicit Renderer(VulkanDevice& vk);
    bool init();

    static void destroyTarget(wlr_addon* addon);
    int64_t gpuToMonotonic(uint64_t ticks);
    void releaseTextures();
    static const wlr_addon_interface s_targetAddon;

    // wlr_renderer come primo membro di una struttura semplice: dal
    // puntatore che wlroots ci passa si risale al Renderer.
    struct Shim {
        wlr_renderer base;
        Renderer* self;
    };
    Shim m_shim {};

    VulkanDevice& m_vk;

    VkCommandPool m_pool = VK_NULL_HANDLE;
    struct Commands {
        VkCommandBuffer cmd;
        uint64_t point; // 0: in registrazione o libero
        bool busy;
    };
    std::vector<Commands> m_commands;
    VkCommandBuffer m_upload = VK_NULL_HANDLE;

    VkSemaphore m_timeline = VK_NULL_HANDLE;
    uint64_t m_lastPoint = 0;
    uint64_t m_completed = 0;
    std::vector<VkSemaphore> m_freeWaitSemaphores;
    std::vector<VkSemaphore> m_freeReleaseSemaphores;
    struct Deferred {
        uint64_t point;
        std::function<void()> fn;
    };
    std::vector<Deferred> m_deferred;

    struct StagingChunk {
        VkBuffer buffer;
        VkDeviceMemory memory;
        uint8_t* data;
        VkDeviceSize size;
        VkDeviceSize used;
        uint64_t point; // ultimo invio che lo legge; UINT64_MAX: in uso da comandi non ancora inviati
    };
    std::vector<StagingChunk> m_staging;

    wlr_drm_syncobj_timeline* m_syncTimeline = nullptr;
    uint64_t m_syncPoint = 0;

    static constexpr int timingSlots = 64;
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    uint64_t m_timing[timingSlots] {}; // punto della timeline di ogni slot; UINT64_MAX: in registrazione
    int m_nextTiming = 0;
    // Calibrazione: lo stesso istante letto sulla GPU e su CLOCK_MONOTONIC.
    uint64_t m_calibrationTicks = 0;
    int64_t m_calibrationNs = 0;
    int64_t m_calibratedAt = 0;

    std::vector<RenderTarget*> m_targets;
    std::unordered_set<Texture*> m_textures;
    wlr_drm_format_set m_shmFormats {};

    VkSampler m_nearest = VK_NULL_HANDLE;
    VkSampler m_linear = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkShaderModule m_vert = VK_NULL_HANDLE;
    VkShaderModule m_textureFrag = VK_NULL_HANDLE;
    VkShaderModule m_rectFrag = VK_NULL_HANDLE;
    VkShaderModule m_shadowFrag = VK_NULL_HANDLE;
    std::map<std::tuple<VkFormat, PipelineKind, bool>, VkPipeline> m_pipelines;
    VkShaderModule m_blurDownFrag = VK_NULL_HANDLE;
    VkShaderModule m_blurUpFrag = VK_NULL_HANDLE;
    VkShaderModule m_blurMixFrag = VK_NULL_HANDLE;
    BlurImage m_blur[blurLevels];
    void destroyBlurImage(BlurImage& image);
};

// Le costanti di ogni disegno (vedi shaders/push.glsl): 112 byte, sotto i
// 128 che ogni GPU garantisce.
struct QuadPush {
    float dst[4]; // x, y, larghezza, altezza in pixel della destinazione
    float target[2]; // dimensioni della destinazione
    float alpha;
    uint32_t flags; // bit 0: ingrandimento bicubico; bit 1: ritaglio arrotondato
    float uvOrigin[2]; // coordinate texture dell'angolo in alto a sinistra
    float uvX[2]; // spostamento lungo il bordo superiore
    float uvY[2]; // spostamento lungo il bordo sinistro
    float pad[2];
    float color[4]; // rettangoli e ombre: colore lineare premoltiplicato
    float shapeRect[4]; // rettangolo arrotondato (ritaglio, o chi proietta l'ombra)
    float shape[4]; // raggio degli angoli, sigma dell'ombra
};
static_assert(sizeof(QuadPush) == 112);

} // namespace vela::render
