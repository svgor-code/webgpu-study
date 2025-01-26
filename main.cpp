#include <GLFW/glfw3.h>
#include <webgpu/webgpu_glfw.h>
#include <iostream>

#include "webgpu-utils.h"

class Application
{
public:
  bool Initialize();

  void Terminate();

  void MainLoop();

  bool IsRunning();

private:
  WGPUTextureView GetNextSurfaceTextureView();

private:
  GLFWwindow *window;
  WGPUDevice device;
  WGPUQueue queue;
  WGPUSurface surface;
};

const uint32_t kWidth = 512;
const uint32_t kHeight = 512;

int main(int, char **)
{

  Application app;

  if (!app.Initialize())
  {
    return 1;
  }

#ifdef __EMSCRIPTEN__
  // Equivalent of the main loop when using Emscripten:
  auto callback = [](void *arg)
  {
    //                   ^^^ 2. We get the address of the app in the callback.
    Application *pApp = reinterpret_cast<Application *>(arg);
    //                  ^^^^^^^^^^^^^^^^ 3. We force this address to be interpreted
    //                                      as a pointer to an Application object.
    pApp->MainLoop(); // 4. We can use the application object
  };
  emscripten_set_main_loop_arg(callback, &app, 0, true);
  //                                     ^^^^ 1. We pass the address of our application object.
#else  // __EMSCRIPTEN__
  while (app.IsRunning())
  {
    app.MainLoop();
  }
#endif // __EMSCRIPTEN__

  app.Terminate();

  return 0;
}

bool Application::Initialize()
{
  glfwInit();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  window = glfwCreateWindow(kWidth, kHeight, "Test", nullptr, nullptr);

  WGPUInstance instance = createInstance();

  std::cout << "Requesting adapter..." << std::endl;
  surface = wgpuGlfwCreateSurfaceForWindow(instance, window);

  WGPURequestAdapterOptions adapterOpts = {};
  adapterOpts.nextInChain = nullptr;
  adapterOpts.compatibleSurface = surface;

  WGPUAdapter adapter = requestAdapterSync(instance, &adapterOpts);

  std::cout << "Got adapter: " << adapter << std::endl;

  wgpuInstanceRelease(instance);

  std::cout << "Requesting device..." << std::endl;

  // WGPUFeatureName* requiredFeatures = [WGPUFeatureName_BGRA8UnormStorage];

  WGPUDeviceDescriptor deviceDesc = {};
  deviceDesc.nextInChain = nullptr;
  // deviceDesc.label = {
  //     .data = "My Device",
  //     .length = size_t("My Device")};
  deviceDesc.requiredFeatureCount = 1; // we do not require any specific feature
  deviceDesc.requiredFeatures = WGPUFeatureName_BGRA8UnormStorage;
  deviceDesc.requiredLimits = nullptr; // we do not require any specific limit
  deviceDesc.defaultQueue.nextInChain = nullptr;
  // deviceDesc.defaultQueue.label = {
  //     .data = "The default queue",
  //     .length = size_t("The default queue")};
  deviceDesc.deviceLostCallbackInfo.nextInChain = nullptr;
  deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
  deviceDesc.deviceLostCallbackInfo.callback = [](WGPUDevice const *device, WGPUDeviceLostReason reason, struct WGPUStringView message, void *userdata1, void *userdata2)
  {
    std::cout << "Device lost: reason " << reason;
    if (message.data)
      std::cout << " (" << message.data << ")";
    std::cout << std::endl;
  };
  deviceDesc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const *device, WGPUErrorType type, struct WGPUStringView message, void *userdata1, void *userdata2)
  {
    std::cout << "Uncaptured device error: type " << type;
    if (message.data)
      std::cout << " (" << message.data << ")";
    std::cout << std::endl;
  };

  device = requestDeviceSync(adapter, &deviceDesc);
  std::cout << "Got device: " << device << std::endl;

  queue = wgpuDeviceGetQueue(device);

  WGPUSurfaceCapabilities capabilities;
  wgpuSurfaceGetCapabilities(surface, adapter, &capabilities);
  WGPUSurfaceConfiguration config = {};
  config.nextInChain = nullptr;

  for (int i = sizeof(capabilities.formats); i >= 0; i--)
  {
    std::cout << "Format: " << capabilities.formats[i] << std::endl;
  }

  // Configuration of the textures created for the underlying swap chain
  config.width = 640;
  config.height = 480;
  config.usage = WGPUTextureUsage_RenderAttachment;
  config.format = WGPUTextureFormat_BGRA8UnormSrgb;

  // And we do not need any particular view format:
  config.viewFormatCount = 0;
  config.viewFormats = nullptr;
  config.device = device;
  config.presentMode = WGPUPresentMode_Fifo;
  config.alphaMode = WGPUCompositeAlphaMode_Auto;

  wgpuSurfaceConfigure(surface, &config);

  wgpuAdapterRelease(adapter);

  return true;
}

void Application::Terminate()
{
  wgpuQueueRelease(queue);
  wgpuSurfaceRelease(surface);
  wgpuDeviceRelease(device);
  glfwDestroyWindow(window);
  glfwTerminate();
}

void Application::MainLoop()
{
  glfwPollEvents();

  WGPUTextureView targetView = GetNextSurfaceTextureView();
  if (!targetView)
  {
    std::cerr << "Failed to acquire next surface texture view." << std::endl;
    return;
  }

  WGPUCommandEncoderDescriptor encoderDesc = {};
  encoderDesc.nextInChain = nullptr;
  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
  if (!encoder)
  {
    std::cerr << "Failed to create command encoder." << std::endl;
    wgpuTextureViewRelease(targetView);
    return;
  }

  WGPURenderPassColorAttachment renderPassColorAttachment = {};
  renderPassColorAttachment.view = targetView;
  renderPassColorAttachment.resolveTarget = nullptr;
  renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
  renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
  renderPassColorAttachment.clearValue = WGPUColor{0.1, 0.4, 0.2, 1.0};

  WGPURenderPassDescriptor renderPassDesc = {};
  renderPassDesc.nextInChain = nullptr;
  renderPassDesc.colorAttachmentCount = 1;
  renderPassDesc.colorAttachments = &renderPassColorAttachment;
  renderPassDesc.depthStencilAttachment = nullptr;
  renderPassDesc.timestampWrites = nullptr;

  WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
  if (!renderPass)
  {
    std::cerr << "Failed to begin render pass." << std::endl;
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(targetView);
    return;
  }

  wgpuRenderPassEncoderEnd(renderPass);
  wgpuRenderPassEncoderRelease(renderPass);

  WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
  cmdBufferDescriptor.nextInChain = nullptr;
  WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDescriptor);
  wgpuCommandEncoderRelease(encoder);

  if (!command)
  {
    std::cerr << "Failed to finish command encoding." << std::endl;
    wgpuTextureViewRelease(targetView);
    return;
  }

  std::cout << "Submitting command..." << std::endl;
  wgpuQueueSubmit(queue, 1, &command);
  std::cout << "Command submitted." << std::endl;

  wgpuCommandBufferRelease(command);
  wgpuTextureViewRelease(targetView);

#ifndef __EMSCRIPTEN__
  wgpuSurfacePresent(surface);
#endif

#if defined(WEBGPU_BACKEND_DAWN)
  wgpuDeviceTick(device);
#elif defined(WEBGPU_BACKEND_WGPU)
  wgpuDevicePoll(device, false, nullptr);
#endif
}

bool Application::IsRunning()
{
  return !glfwWindowShouldClose(window);
}

WGPUTextureView Application::GetNextSurfaceTextureView()
{
  WGPUSurfaceTexture surfaceTexture;
  wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);

  std::cout << surfaceTexture.status << " Status" << std::endl;

  if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
  {
    return nullptr;
  }

  WGPUTextureViewDescriptor viewDescriptor;
  viewDescriptor.nextInChain = nullptr;
  viewDescriptor.format = wgpuTextureGetFormat(surfaceTexture.texture);
  viewDescriptor.dimension = WGPUTextureViewDimension_2D;
  viewDescriptor.baseMipLevel = 0;
  viewDescriptor.mipLevelCount = 1;
  viewDescriptor.baseArrayLayer = 0;
  viewDescriptor.arrayLayerCount = 1;
  viewDescriptor.aspect = WGPUTextureAspect_All;

  std::cout << viewDescriptor.format << std::endl;

  WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);

#ifndef WEBGPU_BACKEND_WGPU
  // We no longer need the texture, only its view
  // (NB: with wgpu-native, surface textures must not be manually released)
  wgpuTextureRelease(surfaceTexture.texture);
#endif // WEBGPU_BACKEND_WGPU

  return targetView;
}