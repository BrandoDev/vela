#include "server.hpp"
#include "settings.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>

// Accessibilità e colore dello schermo, come Windows 11:
//
// - Luce notturna: i colori più caldi la sera, a mano (impostazioni
//   rapide) o pianificata, dalle-alle oppure dal tramonto all'alba (le ore
//   del sole si calcolano dalle coordinate del fuso orario, senza rete).
// - Filtri colore: scala di grigi e le correzioni per i daltonismi.
// - Lente di ingrandimento: Win+più ingrandisce lo schermo del cursore
//   attorno a lui, Win+meno riduce, Win+Esc chiude; la zona ingrandita
//   segue il cursore quando arriva ai bordi.
// - Tasti permanenti: Maiusc, Ctrl, Alt e Win premuti e lasciati valgono
//   per il tasto dopo; premuti due volte restano finché non si ripremono.
//
// Luce notturna e filtri li applica il renderer a tutto ciò che disegna
// (una matrice di colore, docs/renderer.md §7); la lente è la scena
// disegnata a una scala più grande.

namespace vela {

namespace {

// --------------------------------------------------------------- colori --

float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Il bianco di un corpo nero a `kelvin` (Tanner Helland), in sRGB 0-1.
void blackbody(double kelvin, double out[3])
{
    const double t = kelvin / 100.0;
    double r = 255.0;
    double g = 0.0;
    double b = 255.0;
    if (t <= 66.0) {
        g = 99.4708025861 * std::log(t) - 161.1195681661;
        b = t <= 19.0 ? 0.0 : 138.5177312231 * std::log(t - 10.0) - 305.0447927307;
    } else {
        r = 329.698727446 * std::pow(t - 60.0, -0.1332047592);
        g = 288.1221695283 * std::pow(t - 60.0, -0.0755148492);
    }
    out[0] = std::clamp(r, 0.0, 255.0) / 255.0;
    out[1] = std::clamp(g, 0.0, 255.0) / 255.0;
    out[2] = std::clamp(b, 0.0, 255.0) / 255.0;
}

// Quanto resta di rosso, verde e blu (in luce lineare) a quella
// temperatura, rispetto al bianco normale dello schermo (6500 K).
void nightGains(double kelvin, float out[3])
{
    double white[3];
    double warm[3];
    blackbody(6500.0, white);
    blackbody(kelvin, warm);
    for (int i = 0; i < 3; ++i) {
        const float reference = srgbToLinear(float(white[i]));
        out[i] = std::clamp(srgbToLinear(float(warm[i])) / std::max(reference, 1e-4f), 0.0f, 1.0f);
    }
}

using Matrix = std::array<float, 9>;

constexpr Matrix identity { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

Matrix multiply(const Matrix& a, const Matrix& b)
{
    Matrix out {};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k) {
                sum += a[r * 3 + k] * b[k * 3 + c];
            }
            out[r * 3 + c] = sum;
        }
    }
    return out;
}

// Correzione dei daltonismi (daltonizzazione): ciò che chi ha quel
// daltonismo perde (la differenza dalla simulazione di Machado, Oliveira
// e Fernandes 2009, gravità piena) si sposta sui canali che vede.
Matrix daltonize(const Matrix& simulation, const Matrix& shift)
{
    Matrix lost {};
    for (int i = 0; i < 9; ++i) {
        lost[i] = identity[i] - simulation[i];
    }
    const Matrix moved = multiply(shift, lost);
    Matrix out {};
    for (int i = 0; i < 9; ++i) {
        out[i] = identity[i] + moved[i];
    }
    return out;
}

Matrix filterMatrix(const std::string& kind)
{
    // Rosso e verde persi finiscono su verde e blu; il blu perso sul rosso e sul verde.
    constexpr Matrix redGreenShift { 0, 0, 0, 0.7f, 1, 0, 0.7f, 0, 1 };
    constexpr Matrix blueShift { 1, 0, 0.7f, 0, 1, 0.7f, 0, 0, 0 };
    if (kind == "deuteranopia") {
        return daltonize({ 0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f, -0.011820f, 0.042940f,
                             0.968881f },
            redGreenShift);
    }
    if (kind == "protanopia") {
        return daltonize({ 0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f, -0.003882f, -0.048116f,
                             1.051998f },
            redGreenShift);
    }
    if (kind == "tritanopia") {
        return daltonize({ 1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f, 0.004733f, 0.691367f,
                             0.303900f },
            blueShift);
    }
    // Scala di grigi: la luminanza su tutti e tre i canali.
    return { 0.2126f, 0.7152f, 0.0722f, 0.2126f, 0.7152f, 0.0722f, 0.2126f, 0.7152f, 0.0722f };
}

// L'intensità di Windows (0-100) in gradi: da 6500 K (spenta) fino a 1700 K.
double nightKelvin(int strength)
{
    return 6500.0 - std::clamp(strength, 0, 100) / 100.0 * (6500.0 - 1700.0);
}

// ------------------------------------------------------------------ sole --

// Le coordinate del fuso orario del sistema, da zone1970.tab (o zone.tab):
// "+4154+01229" per Europe/Rome. Bastano per le ore del sole.
bool timezoneCoordinates(double& latitude, double& longitude)
{
    std::string zone;
    if (const char* tz = std::getenv("TZ"); tz && *tz) {
        zone = tz[0] == ':' ? tz + 1 : tz;
    } else {
        char target[512] = {};
        const ssize_t n = readlink("/etc/localtime", target, sizeof(target) - 1);
        if (n <= 0) {
            return false;
        }
        zone = std::string(target, size_t(n));
        const size_t at = zone.find("zoneinfo/");
        if (at == std::string::npos) {
            return false;
        }
        zone = zone.substr(at + 9);
    }
    // ±GGPP o ±GGPPSS, latitudine e longitudine di seguito.
    auto parse = [](const std::string& text, size_t& pos, int degreeDigits, double& value) {
        if (pos >= text.size() || (text[pos] != '+' && text[pos] != '-')) {
            return false;
        }
        const double sign = text[pos] == '-' ? -1.0 : 1.0;
        size_t end = pos + 1;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        const std::string digits = text.substr(pos + 1, end - pos - 1);
        if (int(digits.size()) < degreeDigits + 2) {
            return false;
        }
        const double degrees = std::stod(digits.substr(0, size_t(degreeDigits)));
        const double minutes = std::stod(digits.substr(size_t(degreeDigits), 2));
        const double seconds = int(digits.size()) >= degreeDigits + 4 ? std::stod(digits.substr(size_t(degreeDigits) + 2, 2)) : 0.0;
        value = sign * (degrees + minutes / 60.0 + seconds / 3600.0);
        pos = end;
        return true;
    };
    for (const char* table : { "/usr/share/zoneinfo/zone1970.tab", "/usr/share/zoneinfo/zone.tab" }) {
        std::ifstream in(table);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            std::istringstream fields(line);
            std::string codes, coordinates, name;
            if (!(fields >> codes >> coordinates >> name) || name != zone) {
                continue;
            }
            size_t pos = 0;
            return parse(coordinates, pos, 2, latitude) && parse(coordinates, pos, 3, longitude);
        }
    }
    return false;
}

// L'ora locale (minuti dalla mezzanotte) dell'alba o del tramonto di oggi,
// con l'algoritmo dell'"Almanac for Computers" (US Naval Observatory).
// -1: il sole quel giorno non sorge o non tramonta.
int sunTime(bool sunrise, const tm& today, double latitude, double longitude)
{
    constexpr double pi = 3.14159265358979323846;
    auto rad = [](double d) { return d * pi / 180.0; };
    auto deg = [](double r) { return r * 180.0 / pi; };
    auto wrap = [](double v, double range) { return v - range * std::floor(v / range); };

    const double day = today.tm_yday + 1;
    const double lngHour = longitude / 15.0;
    const double t = day + ((sunrise ? 6.0 : 18.0) - lngHour) / 24.0;
    const double anomaly = 0.9856 * t - 3.289;
    const double trueLong = wrap(anomaly + 1.916 * std::sin(rad(anomaly)) + 0.020 * std::sin(rad(2 * anomaly)) + 282.634, 360.0);
    double ascension = wrap(deg(std::atan(0.91764 * std::tan(rad(trueLong)))), 360.0);
    ascension += std::floor(trueLong / 90.0) * 90.0 - std::floor(ascension / 90.0) * 90.0;
    ascension /= 15.0;
    const double sinDec = 0.39782 * std::sin(rad(trueLong));
    const double cosDec = std::cos(std::asin(sinDec));
    const double cosHour = (std::cos(rad(90.833)) - sinDec * std::sin(rad(latitude))) / (cosDec * std::cos(rad(latitude)));
    if (cosHour > 1.0 || cosHour < -1.0) {
        return -1;
    }
    double hour = sunrise ? 360.0 - deg(std::acos(cosHour)) : deg(std::acos(cosHour));
    hour /= 15.0;
    const double local = hour + ascension - 0.06571 * t - 6.622;
    const double utc = wrap(local - lngHour, 24.0);
    const double offsetHours = double(today.tm_gmtoff) / 3600.0;
    return int(std::lround(wrap(utc + offsetHours, 24.0) * 60.0)) % (24 * 60);
}

// "21:30" -> minuti dalla mezzanotte.
int parseClock(const std::string& text, int fallback)
{
    int h = 0;
    int m = 0;
    if (std::sscanf(text.c_str(), "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) {
        return h * 60 + m;
    }
    return fallback;
}

bool inRange(int now, int from, int to)
{
    return from <= to ? (now >= from && now < to) : (now >= from || now < to);
}

uint32_t stickyBit(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
        return WLR_MODIFIER_SHIFT;
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
        return WLR_MODIFIER_CTRL;
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R:
        return WLR_MODIFIER_ALT;
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
        return WLR_MODIFIER_LOGO;
    default:
        return 0;
    }
}

constexpr uint32_t stickyMask = WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO;

const char* yesNo(bool on)
{
    return on ? "sì" : "no";
}

} // namespace

void Server::initAccessibility()
{
    sceneGraph->tearingControl = wlr_tearing_control_manager_v1_create(display, 1);
    loadAccessibilitySettings();
    // La pianificazione della Luce notturna: un controllo al minuto basta
    // (e dopo una sospensione si rimette subito in pari).
    a11y.timer = wl_event_loop_add_timer(
        loop,
        [](void* data) {
            auto* self = static_cast<Server*>(data);
            self->checkNightSchedule();
            wl_event_source_timer_update(self->a11y.timer, 60'000);
            return 0;
        },
        this);
    wl_event_source_timer_update(a11y.timer, 60'000);
}

void Server::loadAccessibilitySettings()
{
    const Settings settings = readSettings();
    a11y.nightStrength = std::clamp(std::atoi(setting(settings, "luce-notturna-intensita", "48").c_str()), 0, 100);
    a11y.schedule = setting(settings, "luce-notturna-pianifica", "no");
    a11y.nightFrom = parseClock(setting(settings, "luce-notturna-dalle"), 21 * 60);
    a11y.nightTo = parseClock(setting(settings, "luce-notturna-alle"), 7 * 60);
    a11y.colorFilterKind = setting(settings, "filtro-colore", "grigi");
    a11y.colorFilterShortcut = settingFlag(settings, "filtri-colore-scorciatoia", false);
    a11y.zoomStep = std::clamp(std::atoi(setting(settings, "lente-incremento", "100").c_str()), 25, 400);
    sceneGraph->allowTearing = settingFlag(settings, "tearing", true) && !(std::getenv("VELA_TEARING")
        && std::strcmp(std::getenv("VELA_TEARING"), "0") == 0);

    const bool night = settingFlag(settings, "luce-notturna", false);
    const bool filter = settingFlag(settings, "filtri-colore", false);
    const bool sticky = settingFlag(settings, "tasti-permanenti", false);
    // Una pianificazione nuova (o cambiata) decide subito; altrimenti vale
    // ciò che c'è nel file (anche se a mano l'utente ha scelto diversamente
    // dalla pianificazione, fino al suo prossimo passaggio).
    const std::string key = a11y.schedule + " " + std::to_string(a11y.nightFrom) + " " + std::to_string(a11y.nightTo);
    const bool scheduleChanged = key != a11y.scheduleKey;
    a11y.scheduleKey = key;
    if (scheduleChanged) {
        a11y.scheduled = -1;
    }
    a11y.nightLight = night;
    a11y.colorFilter = filter;
    a11y.stickyKeys = sticky;
    if (!sticky) {
        a11y.latched = 0;
        a11y.locked = 0;
    }
    checkNightSchedule();
    // All'avvio niente passaggio graduale: com'era.
    if (a11y.nightLevel == 0.0 && a11y.nightLight && !a11y.levelAnimating && m_animationNowMs == 0.0) {
        a11y.nightLevel = 1.0;
    }
    if (a11y.nightLight != (a11y.nightLevel > 0.5) && !a11y.levelAnimating) {
        a11y.levelFrom = a11y.nightLevel;
        a11y.levelTween = Tween(1000.0, &motion::decelerate);
        a11y.levelAnimating = true;
        scheduleFrames();
    }
    applyColorFilter();
    announceAccessibility();
}

void Server::checkNightSchedule()
{
    if (a11y.schedule != "tramonto" && a11y.schedule != "ore") {
        a11y.scheduled = -1;
        return;
    }
    const time_t now = std::time(nullptr);
    tm local {};
    localtime_r(&now, &local);
    const int minutes = local.tm_hour * 60 + local.tm_min;
    int from = a11y.nightFrom;
    int to = a11y.nightTo;
    if (a11y.schedule == "tramonto") {
        double latitude = 0.0;
        double longitude = 0.0;
        if (timezoneCoordinates(latitude, longitude)) {
            const int sunset = sunTime(false, local, latitude, longitude);
            const int sunrise = sunTime(true, local, latitude, longitude);
            if (sunset >= 0 && sunrise >= 0) {
                from = sunset;
                to = sunrise;
            }
        }
    }
    const int wanted = inRange(minutes, from, to) ? 1 : 0;
    if (wanted == a11y.scheduled) {
        return; // nessun passaggio: resta la scelta fatta a mano
    }
    a11y.scheduled = wanted;
    if (bool(wanted) != a11y.nightLight) {
        wlr_log(WLR_INFO, "Luce notturna: %s dalla pianificazione (%02d:%02d-%02d:%02d)", wanted ? "accesa" : "spenta",
            from / 60, from % 60, to / 60, to % 60);
        setNightLight(bool(wanted));
    }
}

void Server::setNightLight(bool on, bool save)
{
    if (save) {
        writeSetting("luce-notturna", yesNo(on));
    }
    if (on == a11y.nightLight && !a11y.levelAnimating && a11y.nightLevel == (on ? 1.0 : 0.0)) {
        return;
    }
    a11y.nightLight = on;
    // Come Windows: si scalda (o si raffredda) in un secondo.
    a11y.levelFrom = a11y.nightLevel;
    a11y.levelTween = Tween(1000.0, &motion::decelerate);
    a11y.levelAnimating = true;
    scheduleFrames();
    announceAccessibility();
}

void Server::setColorFilter(bool on, bool save)
{
    if (save) {
        writeSetting("filtri-colore", yesNo(on));
    }
    if (on == a11y.colorFilter) {
        return;
    }
    a11y.colorFilter = on;
    applyColorFilter();
    announceAccessibility();
}

void Server::setStickyKeys(bool on, bool save)
{
    if (save) {
        writeSetting("tasti-permanenti", yesNo(on));
    }
    if (on == a11y.stickyKeys) {
        return;
    }
    a11y.stickyKeys = on;
    if (!on && (a11y.latched || a11y.locked)) {
        a11y.latched = 0;
        a11y.locked = 0;
        if (wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat)) {
            wlr_keyboard_notify_modifiers(keyboard, keyboard->modifiers.depressed, 0,
                keyboard->modifiers.locked & ~stickyMask, keyboard->modifiers.group);
        }
    }
    a11y.candidate = 0;
    announceAccessibility();
}

void Server::applyColorFilter()
{
    // I filtri colore: sempre nel disegno.
    Matrix m = identity;
    if (a11y.colorFilter) {
        m = filterMatrix(a11y.colorFilterKind);
    }
    // La Luce notturna: nella gamma del monitor se si può (OutputFrame).
    float gains[3] { 1.0f, 1.0f, 1.0f };
    const bool night = a11y.nightLevel > 0.0;
    if (night) {
        nightGains(6500.0 + (nightKelvin(a11y.nightStrength) - 6500.0) * a11y.nightLevel, gains);
    }
    const bool filterChanged = a11y.colorFilter != sceneGraph->colorFiltered
        || (a11y.colorFilter && !std::equal(m.begin(), m.end(), sceneGraph->colorFilter));
    const bool nightChanged = night != sceneGraph->nightActive
        || !std::equal(std::begin(gains), std::end(gains), sceneGraph->nightGains);
    sceneGraph->colorFiltered = a11y.colorFilter;
    std::copy(m.begin(), m.end(), sceneGraph->colorFilter);
    sceneGraph->nightActive = night;
    std::copy(std::begin(gains), std::end(gains), sceneGraph->nightGains);
    if (nightChanged) {
        ++sceneGraph->nightVersion;
    }
    if (filterChanged || nightChanged) {
        // Dove il colore passa dal disegno, tutto va ridisegnato (dove passa
        // dalla gamma basta il commit, che il frame fa comunque).
        for (Output* output : outputs) {
            output->sceneFrame->damageWhole();
        }
    }
}

bool Server::tickAccessibility(double nowMs)
{
    bool running = false;
    if (a11y.levelAnimating) {
        const double p = a11y.levelTween.progress(nowMs);
        const double target = a11y.nightLight ? 1.0 : 0.0;
        a11y.nightLevel = a11y.levelFrom + (target - a11y.levelFrom) * p;
        if (a11y.levelTween.finished(nowMs)) {
            a11y.nightLevel = target;
            a11y.levelAnimating = false;
        }
        applyColorFilter();
        running = running || a11y.levelAnimating;
    }
    if (a11y.zoomAnimating) {
        const double p = a11y.zoomTween.progress(nowMs);
        const double before = a11y.zoom;
        a11y.zoom = a11y.zoomFrom + (a11y.zoomTarget - a11y.zoomFrom) * p;
        if (a11y.zoomTween.finished(nowMs)) {
            a11y.zoom = a11y.zoomTarget;
            a11y.zoomAnimating = false;
        }
        // Il punto sotto il cursore resta fermo sullo schermo.
        if (a11y.zoomOutput && before > 0.0 && a11y.zoom > 0.0) {
            a11y.viewX = cursor->x - (cursor->x - a11y.viewX) * before / a11y.zoom;
            a11y.viewY = cursor->y - (cursor->y - a11y.viewY) * before / a11y.zoom;
        }
        if (!a11y.zoomAnimating && a11y.zoom <= 1.0 && !a11y.magnifier) {
            a11y.zoom = 1.0;
        }
        updateMagnifier();
        running = running || a11y.zoomAnimating;
    }
    return running;
}

// ----------------------------------------------------------------- lente --

void Server::setMagnifier(bool on)
{
    if (on == a11y.magnifier) {
        return;
    }
    a11y.magnifier = on;
    if (on) {
        // Come Windows: si parte dal doppio.
        a11y.zoomTarget = 1.0 + a11y.zoomStep / 100.0;
    } else {
        a11y.zoomTarget = 1.0;
    }
    a11y.zoomFrom = a11y.zoom;
    a11y.zoomTween = Tween(motion::magnifierMs, &motion::decelerate);
    a11y.zoomAnimating = true;
    if (on && !a11y.zoomOutput) {
        a11y.zoomOutput = outputAt(cursor->x, cursor->y);
        a11y.viewX = a11y.zoomOutput ? a11y.zoomOutput->box().x : 0.0;
        a11y.viewY = a11y.zoomOutput ? a11y.zoomOutput->box().y : 0.0;
    }
    wlr_log(WLR_INFO, "Lente di ingrandimento %s", on ? "aperta" : "chiusa");
    scheduleFrames();
    announceAccessibility();
}

void Server::zoomMagnifier(int direction)
{
    if (!a11y.magnifier) {
        if (direction > 0) {
            setMagnifier(true);
        }
        return;
    }
    const double step = a11y.zoomStep / 100.0;
    const double target = std::clamp(a11y.zoomTarget + direction * step, 1.0, 16.0);
    if (target == a11y.zoomTarget) {
        return;
    }
    a11y.zoomTarget = target;
    a11y.zoomFrom = a11y.zoom;
    a11y.zoomTween = Tween(motion::magnifierMs, &motion::decelerate);
    a11y.zoomAnimating = true;
    scheduleFrames();
}

void Server::updateMagnifier()
{
    Output* out = outputAt(cursor->x, cursor->y);
    auto restoreCursor = [this](Output* output) {
        // Il cursore torna dove lo mette wlr_cursor (punto logico dello schermo).
        const wlr_box box = output->box();
        wlr_output_cursor* c;
        wl_list_for_each(c, &output->wlr->cursors, link)
        {
            wlr_output_cursor_move(c, cursor->x - box.x, cursor->y - box.y);
        }
    };
    const bool zoomed = a11y.zoom > 1.0;
    for (Output* output : outputs) {
        if (output != out || !zoomed) {
            if (output->sceneFrame->zoom() > 1.0) {
                output->sceneFrame->setMagnifier(1.0, 0.0, 0.0);
                restoreCursor(output);
            }
        }
    }
    if (!zoomed || !out) {
        a11y.zoomOutput = zoomed ? nullptr : a11y.zoomOutput;
        return;
    }
    const wlr_box box = out->box();
    if (out != a11y.zoomOutput) {
        // Un altro schermo: la zona ingrandita parte attorno al cursore.
        a11y.zoomOutput = out;
        a11y.viewX = cursor->x - (cursor->x - box.x) / a11y.zoom;
        a11y.viewY = cursor->y - (cursor->y - box.y) / a11y.zoom;
    }
    const double w = box.width / a11y.zoom;
    const double h = box.height / a11y.zoom;
    // Il cursore spinge la zona quando arriva ai suoi bordi.
    a11y.viewX = std::clamp(a11y.viewX, cursor->x - w, cursor->x);
    a11y.viewY = std::clamp(a11y.viewY, cursor->y - h, cursor->y);
    a11y.viewX = std::clamp(a11y.viewX, double(box.x), box.x + box.width - w);
    a11y.viewY = std::clamp(a11y.viewY, double(box.y), box.y + box.height - h);
    out->sceneFrame->setMagnifier(a11y.zoom, a11y.viewX, a11y.viewY);
    // Il cursore dove si vede il punto che indica.
    wlr_output_cursor* c;
    wl_list_for_each(c, &out->wlr->cursors, link)
    {
        wlr_output_cursor_move(c, (cursor->x - a11y.viewX) * a11y.zoom, (cursor->y - a11y.viewY) * a11y.zoom);
    }
}

// ------------------------------------------------------ tasti permanenti --

void Server::stickyKey(Keyboard& keyboard, const xkb_keysym_t* syms, int count, bool pressed)
{
    if (!a11y.stickyKeys) {
        return;
    }
    uint32_t bit = 0;
    for (int i = 0; i < count; ++i) {
        bit |= stickyBit(syms[i]);
    }
    wlr_keyboard* k = keyboard.wlr;
    auto apply = [&] {
        wlr_keyboard_notify_modifiers(k, k->modifiers.depressed, a11y.latched,
            (k->modifiers.locked & ~stickyMask) | a11y.locked, k->modifiers.group);
    };
    if (bit) {
        if (pressed) {
            a11y.candidate = bit;
            return;
        }
        if (a11y.candidate != bit) {
            return; // usato con un altro tasto: niente da ricordare
        }
        a11y.candidate = 0;
        if (bit == WLR_MODIFIER_LOGO) {
            // Win: la prima volta resta premuto per il tasto dopo; di
            // nuovo, apre Start (come il tasto da solo).
            if (a11y.latched & bit) {
                a11y.latched &= ~bit;
            } else {
                a11y.latched |= bit;
                superTap = false;
            }
        } else if (a11y.locked & bit) {
            a11y.locked &= ~bit;
        } else if (a11y.latched & bit) {
            a11y.latched &= ~bit;
            a11y.locked |= bit;
        } else {
            a11y.latched |= bit;
        }
        apply();
        return;
    }
    a11y.candidate = 0;
    // Il tasto dopo li ha usati: al suo rilascio si lasciano.
    if (!pressed && a11y.latched) {
        a11y.latched = 0;
        apply();
    }
}

// ------------------------------------------------------------- la shell --

std::string Server::accessibilityJson() const
{
    // Le ore del sole di oggi, per la pagina della Luce notturna ("Dal tramonto all'alba").
    std::string sun;
    double latitude = 0.0;
    double longitude = 0.0;
    if (timezoneCoordinates(latitude, longitude)) {
        const time_t now = std::time(nullptr);
        tm local {};
        localtime_r(&now, &local);
        const int sunset = sunTime(false, local, latitude, longitude);
        const int sunrise = sunTime(true, local, latitude, longitude);
        if (sunset >= 0 && sunrise >= 0) {
            char text[64];
            std::snprintf(text, sizeof text, ",\"sunset\":\"%02d:%02d\",\"sunrise\":\"%02d:%02d\"", sunset / 60,
                sunset % 60, sunrise / 60, sunrise % 60);
            sun = text;
        }
    }
    return std::string("{\"nightLight\":") + (a11y.nightLight ? "true" : "false")
        + ",\"colorFilter\":" + (a11y.colorFilter ? "true" : "false")
        + ",\"magnifier\":" + (a11y.magnifier ? "true" : "false")
        + ",\"stickyKeys\":" + (a11y.stickyKeys ? "true" : "false") + sun + "}";
}

void Server::announceAccessibility()
{
    sendShellCommand("accessibility " + accessibilityJson());
}

} // namespace vela
