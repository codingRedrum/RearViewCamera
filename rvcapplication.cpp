#include "rvcapplication.h"
#include "terminationguard.h"

#include "config.h"

#include <algorithm>
#include <future>
#include <stdexcept>
#include <thread>

namespace togg {
namespace rvc {

namespace {
constexpr auto NUM_FRAME_BUFFERS = 3u;
constexpr display::Window::Size RVC_WINDOW_SIZE { 897, 526 };
constexpr display::Window::Positon RVC_WINDOW_POS { 1032, 57 };
}

RvcApplication::RvcApplication() // NOLINT(cppcoreguidelines-pro-type-member-init)
    : m_eventProvider(m_ppsManager)
    , m_visibilityController(
              m_eventProvider, [this] { cameraOn(); }, [this] { cameraOff(); })
    , m_camManager { std::make_unique<camera::CameraManager>() }
{
}

void RvcApplication::cameraOn()
{
    const std::lock_guard<std::mutex> lock { m_mutex };
    // Don't show the window now, it will be shown
    // when first camera frame will be ready.
    if (m_camera) {
        if (m_camera->state() == camera::CameraFeed::State::Paused) {
            m_camera->resume();
        } else {
            m_camera->start();
        }
    }
}

void RvcApplication::cameraOff()
{
    const std::lock_guard<std::mutex> lock { m_mutex };
    if (m_camera) {
        // Don't stop, just pause the stream to reduce
        // the latency when turning it on back again.
        m_camera->pause();
    }
    hideWindow();
}

int RvcApplication::run()
{
    initialize();

    m_ppsManager.run();
    TerminationGuard tg;

    teardown();
    return 0;
}

void RvcApplication::teardown()
{
    hideWindow();
    if (m_camera) {
        m_camera->stop();
    }
    m_camera.reset();
    m_window.reset();
    m_display.reset();
    m_dev.reset();
}

void RvcApplication::hideWindow()
{
    if (m_window) {
        m_window->hide();
    }
}

std::future<bool> RvcApplication::initCamera(std::promise<camera::CameraInput> inputPromise,
                                             std::future<FrameBuffers> buffers)
{
    return std::async(
            std::launch::async,
            [this](auto inputPromise, auto buffers) {
                camera::CameraInput rvcInput;
                try {
                    auto inputs = m_camManager->queryInputs();
                    auto rvcIt = inputs.find(config::RVC_CAMERA_ID);
                    if (rvcIt == inputs.end()) {
                        throw std::runtime_error("No RVC camera found");
                    }
                    rvcInput = rvcIt->second;
                    inputPromise.set_value(rvcInput);
                    m_camera = std::make_unique<camera::CameraFeed>(rvcInput);
                    m_camera->open(buffers.get(), [&](uint32_t bufferIdx) {
                        if (m_window) {
                            if (m_camera->state() == camera::CameraFeed::State::Started) {
                                m_window->showFrame(bufferIdx);
                            } else {
                                m_window->hide();
                            }
                        }
                    });
                } catch (const std::exception &e) {
                    inputPromise.set_exception(std::current_exception());
                    return false;
                }
                return true;
            },
            std::move(inputPromise), std::move(buffers));
}

std::future<bool> RvcApplication::initDisplay(std::promise<FrameBuffers> buffersPromise,
                                              std::future<camera::CameraInput> inputConfig)
{
    return std::async(
            std::launch::async,
            [this](auto buffersPromise, auto inputConfig) {
                try {
                    // Setup display
                    m_dev = std::make_unique<display::Device>(config::WFD_CLIENT_ID);
                    auto displays = display::Display::availableDisplays(*m_dev);
                    if (displays.empty()) {
                        throw std::runtime_error("No displays found");
                    }
                    m_display = std::make_unique<display::Display>(*m_dev, displays.front());

                    // Setup window and buffers
                    auto windows = display::Window::availableWindows(*m_display);
                    if (windows.empty() || windows.size() <= config::WFD_CAMERA_PIPELINE_IDX) {
                        throw std::runtime_error("Expected WFD window not found");
                    }

                    const auto cameraInput = inputConfig.get();
                    const auto resulution = cameraInput.resolutions().front();
                    const auto pixelFormat = cameraInput.pixelFormats().front();
                    m_window = std::make_unique<display::Window>(
                            *m_display, windows[config::WFD_CAMERA_PIPELINE_IDX], RVC_WINDOW_POS, RVC_WINDOW_SIZE,
                            display::Window::Size { resulution.width, resulution.height }, pixelFormat,
                            NUM_FRAME_BUFFERS);
                    buffersPromise.set_value(m_window->buffers());
                } catch (const std::exception &e) {
                    buffersPromise.set_exception(std::current_exception());
                    return false;
                }
                return true;
            },
            std::move(buffersPromise), std::move(inputConfig));
}

void RvcApplication::initialize()
{
    auto cameraInputPromise = std::promise<camera::CameraInput>();
    auto cameraInputFuture = cameraInputPromise.get_future();

    auto buffersPromise = std::promise<FrameBuffers>();
    auto buffersFuture = buffersPromise.get_future();

    auto cameraInit = initCamera(std::move(cameraInputPromise), std::move(buffersFuture));
    auto displayInit = initDisplay(std::move(buffersPromise), std::move(cameraInputFuture));

    cameraInit.get();
    displayInit.get();
}

} // namespace rvc
} // namespace togg
