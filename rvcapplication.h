#pragma once

#include "camera/camerafeed.h"
#include "camera/cameramanager.h"
#include "display/device.h"
#include "display/display.h"
#include "display/window.h"
#include "eventprovider.h"
#include "ppsmanager.h"
#include "visibilitycontroller.h"

#include <future>
#include <memory>
#include <mutex>

namespace togg {
namespace rvc {

class RvcApplication
{
public:
    RvcApplication();
    int run();

private:
    void initialize();
    std::future<bool> initCamera(std::promise<camera::CameraInput> inputPromise, std::future<FrameBuffers> buffers);
    std::future<bool> initDisplay(std::promise<FrameBuffers> buffersPromise,
                                  std::future<camera::CameraInput> inputConfig);

    void initCamera();
    void cameraOn();
    void cameraOff();

    void teardown();

    void hideWindow();

    std::mutex m_mutex;
    pps::PPSManager m_ppsManager {};
    EventProvider m_eventProvider;
    VisibilityController m_visibilityController;

    std::unique_ptr<camera::CameraFeed> m_camera {};
    std::unique_ptr<camera::CameraManager> m_camManager {};
    std::unique_ptr<display::Device> m_dev {};
    std::unique_ptr<display::Display> m_display {};
    std::unique_ptr<display::Window> m_window {};
};

} // namespace rvc
} // namespace togg
