// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

extern "C" {
#include "diagnostics.h"
#include "sync.h"
}

#include <gtest/gtest.h>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

TEST(SyncPolicy, OnlyProprietary580DefaultsToCompatibility)
{
    EXPECT_FALSE(vela_sync_file_enabled(true, true, 580, nullptr));
    EXPECT_TRUE(vela_sync_file_enabled(true, false, 580, nullptr)); // NVK
    EXPECT_TRUE(vela_sync_file_enabled(true, true, 575, nullptr));
    EXPECT_TRUE(vela_sync_file_enabled(true, true, 590, nullptr));
    EXPECT_TRUE(vela_sync_file_enabled(true, false, 0, nullptr)); // AMD/Intel
}

TEST(SyncPolicy, OverrideCannotEnableUnsupportedDeviceFeatures)
{
    EXPECT_TRUE(vela_sync_file_enabled(true, true, 580, "1"));
    EXPECT_FALSE(vela_sync_file_enabled(true, false, 0, "0"));
    EXPECT_FALSE(vela_sync_file_enabled(false, true, 580, "1"));
    EXPECT_FALSE(vela_sync_file_enabled(true, true, 580, "unexpected"));
}

TEST(SyncFence, CompletedFenceCanBeSkippedWithoutConsumingOrLeakingIt)
{
    struct vela_process_resources before{}, after{};
    ASSERT_TRUE(vela_process_resources(getpid(), &before));
    for (int i = 0; i < 2048; ++i) {
        int fd = eventfd(0, EFD_CLOEXEC);
        ASSERT_GE(fd, 0);
        EXPECT_FALSE(vela_sync_ready(fd));
        eventfd_write(fd, 1);
        EXPECT_TRUE(vela_sync_ready(fd));
        EXPECT_GE(fcntl(fd, F_GETFD), 0); // ready() leaves ownership to caller
        EXPECT_TRUE(vela_sync_wait_close(fd));
        EXPECT_EQ(fcntl(fd, F_GETFD), -1);
    }
    ASSERT_TRUE(vela_process_resources(getpid(), &after));
    EXPECT_EQ(after.fds, before.fds);
}

static volatile sig_atomic_t interrupts;
static void interrupted(int) { interrupts = 1; }

TEST(SyncFence, CpuWaitSurvivesInterruptAndClosesOnlyAfterProducerFinishes)
{
    struct sigaction action{}, old{};
    action.sa_handler = interrupted;
    sigemptyset(&action.sa_mask);
    ASSERT_EQ(sigaction(SIGUSR1, &action, &old), 0);
    interrupts = 0;
    int fd = eventfd(0, EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    pthread_t consumer = pthread_self();
    std::thread producer([fd, consumer] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pthread_kill(consumer, SIGUSR1);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        eventfd_write(fd, 1);
    });
    EXPECT_TRUE(vela_sync_wait_close(fd));
    producer.join();
    EXPECT_GT(interrupts, 0);
    EXPECT_EQ(fcntl(fd, F_GETFD), -1);
    sigaction(SIGUSR1, &old, nullptr);
}

TEST(SyncFence, InvalidDescriptorDoesNotHangOrAppearReady)
{
    EXPECT_FALSE(vela_sync_ready(-1));
    EXPECT_FALSE(vela_sync_wait_close(-1));
    int fd = eventfd(1, EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    close(fd);
    EXPECT_FALSE(vela_sync_ready(fd));
    EXPECT_FALSE(vela_sync_wait_close(fd));
}
