#include "stop_signal.h"

#include <csignal>
#include <stdexcept>
#include <system_error>

#include <unistd.h>

void StopSignal::request() {
    {

        std::lock_guard<std::mutex> lock(mutex_);
        flag_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
}

void StopSignal::wait() const {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return flag_.load(std::memory_order_acquire); });
}

namespace {

std::atomic<int> gSignalPipe{-1};
std::atomic<int> gWatchers{0};

void onSignal(int) {
    const int fd = gSignalPipe.load(std::memory_order_acquire);
    if (fd >= 0) {
        const char byte = 's';

        (void)!::write(fd, &byte, 1);
    }
}

}

SignalWatcher::SignalWatcher(StopSignal& stop) : stop_(stop) {
    if (gWatchers.fetch_add(1) != 0) {
        gWatchers.fetch_sub(1);
        throw std::logic_error("solo puede existir un SignalWatcher a la vez");
    }
    if (::pipe(pipeFds_) != 0) {
        gWatchers.fetch_sub(1);
        throw std::system_error(errno, std::generic_category(), "no se pudo crear el pipe de senales");
    }
    gSignalPipe.store(pipeFds_[1], std::memory_order_release);

    struct sigaction action {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    thread_ = std::thread([this] { watch(); });
}

SignalWatcher::~SignalWatcher() {

    struct sigaction action {};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    gSignalPipe.store(-1, std::memory_order_release);

    const char byte = 'q';
    (void)!::write(pipeFds_[1], &byte, 1);
    if (thread_.joinable()) thread_.join();

    ::close(pipeFds_[0]);
    ::close(pipeFds_[1]);
    gWatchers.fetch_sub(1);
}

void SignalWatcher::watch() {
    char byte = 0;
    for (;;) {
        const ssize_t n = ::read(pipeFds_[0], &byte, 1);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return;
        }
        if (byte == 'q') return;
        received_.fetch_add(1, std::memory_order_acq_rel);
        stop_.request();
    }
}
