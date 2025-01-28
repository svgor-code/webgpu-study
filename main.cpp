#define WEBGPU_CPP_IMPLEMENTATION
#define DAWN_PLATFORM_IS_X11
#include <GLFW/glfw3.h>
#include <webgpu/webgpu_cpp.h>

#include <cassert>
#include <functional>
#include <iostream>

#include "webgpu/webgpu_glfw.h"

using namespace wgpu;

const uint32_t kWidth = 512;
const uint32_t kHeight = 512;

const char* shaderSource = R"(
@vertex
fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4f {
    var p = vec2f(0.0, 0.0);
    if (in_vertex_index == 0u) {
        p = vec2f(-0.5, -0.5);
    } else if (in_vertex_index == 1u) {
        p = vec2f(0.5, -0.5);
    } else {
        p = vec2f(0.0, 0.5);
    }
    return vec4f(p, 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
    return vec4f(0.0, 0.4, 1.0, 1.0);
}
)";

struct UserData {
  Device device;
};

Adapter requestAdapterSync(Instance instance,
                           RequestAdapterOptions* adapterOpts) {
  struct RequestAdapterUserData {
    Adapter adapter;
    bool processFinished;
  };

  RequestAdapterUserData userData;

  auto requestAdapterCallback = [](RequestAdapterStatus status, Adapter adapter,
                                   StringView message,
                                   RequestAdapterUserData* userdata) {
    if (status == RequestAdapterStatus::Success) {
      std::cout << "Lambda: Adapter acquired successfully: "
                << std::string(message.data, message.length) << std::endl;

      userdata->adapter = adapter;
      userdata->processFinished = true;
    } else {
      std::cerr << "Lambda: Failed to acquire adapter. Status: "
                << static_cast<int>(status) << std::endl;
      if (message.data && message.length > 0) {
        std::cerr << "Message: " << std::string(message.data, message.length)
                  << std::endl;
      } else {
        std::cerr << "No additional message provided." << std::endl;
      }
    }
  };

  Future futureRequestAdapter =
      instance.RequestAdapter(adapterOpts, CallbackMode::WaitAnyOnly,
                              requestAdapterCallback, &userData);

  instance.WaitAny(futureRequestAdapter, 0);

  std::cout << "Return adapter" << std::endl;

  return userData.adapter;
}

void requestDeviceSync(Instance instance, Adapter adapter, Device* device) {
  auto onDeviceRequest = [](RequestDeviceStatus status, Device _device,
                            StringView message,
                            Device* device) { *device = _device; };

  DeviceDescriptor deviceDesc = {};
  deviceDesc.nextInChain = nullptr;
  deviceDesc.label = "My Device";
  deviceDesc.requiredFeatureCount = 0;
  deviceDesc.requiredLimits = nullptr;
  deviceDesc.defaultQueue.nextInChain = nullptr;
  deviceDesc.defaultQueue.label = "The default queue";

  Future futureRequestDevice = adapter.RequestDevice(
      &deviceDesc, CallbackMode::WaitAnyOnly, onDeviceRequest, device);

  instance.WaitAny(futureRequestDevice, 0);
}

class Application {
 public:
  // Initialize everything and return true if it went all right
  bool Initialize();

  // Uninitialize everything that was initialized
  void Terminate();

  // Draw a frame and handle events
  void MainLoop();

  // Return true as long as the main loop should keep on running
  bool IsRunning();

 private:
  void InitializePipeline();
  TextureView GetNextSurfaceTextureView();

 private:
  // We put here all the variables that are shared between init and main loop
  GLFWwindow* window;
  Device device;
  Queue queue;
  Surface surface;
  Instance instance;
  UncapturedErrorCallback<> uncapturedErrorCallbackHandle;
  TextureFormat surfaceFormat = TextureFormat::Undefined;
  RenderPipeline pipeline;
};

int main() {
  Application app;

  if (!app.Initialize()) {
    return 1;
  }

  while (app.IsRunning()) {
    app.MainLoop();
  }

  app.Terminate();
  return 0;
}

bool Application::Initialize() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  window = glfwCreateWindow(kWidth, kHeight, "WebGPU window", nullptr, nullptr);

  assert(window);

  instance = CreateInstance(nullptr);
  surface = glfw::CreateSurfaceForWindow(instance, window);

  if (!instance) {
    std::cerr << "Instance creation failed!" << std::endl;
    return false;
  }

  if (!surface) {
    std::cerr << "Surface creation failed!" << std::endl;
    return false;
  }

  RequestAdapterOptions adapterOpts = {};
  // adapterOpts.compatibleSurface = &tempSurface;

  Adapter adapter = requestAdapterSync(instance, &adapterOpts);
  requestDeviceSync(instance, adapter, &device);

  queue = device.GetQueue();
  SurfaceCapabilities surfaceCapabilities;
  surface.GetCapabilities(adapter, &surfaceCapabilities);
  surfaceFormat = surfaceCapabilities.formats[0];

  SurfaceConfiguration config = {};

  config.width = 640;
  config.height = 480;
  config.usage = TextureUsage::RenderAttachment;
  config.format = surfaceFormat;
  config.viewFormatCount = 0;
  config.viewFormats = nullptr;
  config.device = device;
  config.presentMode = PresentMode::Fifo;
  config.alphaMode = CompositeAlphaMode::Auto;

  surface.Configure(&config);

  std::cout << "Initialized" << " - surface " << surface.Get() << std::endl;

  InitializePipeline();

  return true;
}

void Application::MainLoop() {
  glfwPollEvents();

  TextureView targetView = GetNextSurfaceTextureView();
  if (!targetView) return;

  CommandEncoderDescriptor encoderDesc = {};
  encoderDesc.label = "Command Encoder";

  CommandEncoder encoder = device.CreateCommandEncoder(&encoderDesc);

  RenderPassDescriptor renderPassDesc = {};
  RenderPassColorAttachment renderPassColorAttachment = {};
  renderPassColorAttachment.view = targetView;
  renderPassColorAttachment.resolveTarget = nullptr;
  renderPassColorAttachment.loadOp = LoadOp::Clear;
  renderPassColorAttachment.storeOp = StoreOp::Store;
  Color color = {0.9, 0.1, 0.2, 1.0};
  renderPassColorAttachment.clearValue = color;
  renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
  renderPassDesc.colorAttachmentCount = 1;
  renderPassDesc.colorAttachments = &renderPassColorAttachment;
  renderPassDesc.depthStencilAttachment = nullptr;
  renderPassDesc.timestampWrites = nullptr;

  RenderPassEncoder renderPass = encoder.BeginRenderPass(&renderPassDesc);

  // Select which render pipeline to use
  renderPass.SetPipeline(pipeline);
  // Draw 1 instance of a 3-vertices shape
  renderPass.Draw(3, 1, 0, 0);

  renderPass.End();

  CommandBufferDescriptor cmdBufferDescriptor = {};
  cmdBufferDescriptor.label = "Command buffer";
  CommandBuffer command = encoder.Finish(&cmdBufferDescriptor);

  queue.Submit(1, &command);

  surface.Present();
  device.Tick();
}

bool Application::IsRunning() { return !glfwWindowShouldClose(window); }

void Application::Terminate() {
  surface.Unconfigure();
  glfwDestroyWindow(window);
  glfwTerminate();
}

void Application::InitializePipeline() {
  ShaderModuleDescriptor shaderDesc;
  ShaderSourceWGSL shaderSourceWGSL;

  shaderSourceWGSL.code = shaderSource;
  // Connect the chain
  shaderDesc.nextInChain = &shaderSourceWGSL;
#ifdef WEBGPU_BACKEND_WGPU
  shaderDesc.hintCount = 0;
  shaderDesc.hints = nullptr;
#endif
  ShaderModule shaderModule = device.CreateShaderModule(&shaderDesc);

  RenderPipelineDescriptor pipelineDesc;
  pipelineDesc.layout = nullptr;

  pipelineDesc.vertex.bufferCount = 0;
  pipelineDesc.vertex.buffers = nullptr;
  pipelineDesc.vertex.module = shaderModule;
  pipelineDesc.vertex.entryPoint = "vs_main";
  pipelineDesc.vertex.constantCount = 0;
  pipelineDesc.vertex.constants = nullptr;

  pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;
  pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;
  pipelineDesc.primitive.frontFace = FrontFace::CCW;
  pipelineDesc.primitive.cullMode = CullMode::None;

  pipelineDesc.depthStencil = nullptr;

  pipelineDesc.multisample.count = 1;
  pipelineDesc.multisample.mask = ~0u;
  pipelineDesc.multisample.alphaToCoverageEnabled = false;

  BlendState blendState;
  blendState.color.srcFactor = BlendFactor::SrcAlpha;
  blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
  blendState.color.operation = BlendOperation::Add;
  blendState.alpha.srcFactor = BlendFactor::Zero;
  blendState.alpha.dstFactor = BlendFactor::One;
  blendState.alpha.operation = BlendOperation::Add;

  ColorTargetState colorTarget;
  colorTarget.format = surfaceFormat;
  colorTarget.blend = &blendState;
  colorTarget.writeMask = ColorWriteMask::All;

  FragmentState fragmentState;
  fragmentState.module = shaderModule;
  fragmentState.entryPoint = "fs_main";
  fragmentState.constantCount = 0;
  fragmentState.constants = nullptr;
  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;

  pipelineDesc.fragment = &fragmentState;

  pipeline = device.CreateRenderPipeline(&pipelineDesc);
}

TextureView Application::GetNextSurfaceTextureView() {
  if (!surface) {
    std::cerr << "Surface not found!" << " - surface" << surface.Get()
              << std::endl;
    return nullptr;
  }

  SurfaceTexture surfaceTexture;
  surface.GetCurrentTexture(&surfaceTexture);

  if (surfaceTexture.status != SurfaceGetCurrentTextureStatus::Success) {
    return nullptr;
  }

  Texture texture = surfaceTexture.texture;

  // Create a view for this surface texture
  TextureViewDescriptor viewDescriptor;
  viewDescriptor.label = "Surface texture view";
  viewDescriptor.format = texture.GetFormat();
  viewDescriptor.dimension = TextureViewDimension::e2D;
  viewDescriptor.baseMipLevel = 0;
  viewDescriptor.mipLevelCount = 1;
  viewDescriptor.baseArrayLayer = 0;
  viewDescriptor.arrayLayerCount = 1;
  viewDescriptor.aspect = TextureAspect::All;
  viewDescriptor.usage = TextureUsage::RenderAttachment;
  TextureView targetView = texture.CreateView(&viewDescriptor);

  return targetView;
}