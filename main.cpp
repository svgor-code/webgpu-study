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
fn vs_main(@location(0) in_vertex_position: vec2f) -> @builtin(position) vec4f {
	return vec4f(in_vertex_position, 0.0, 1.0);
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

Device requestDeviceSync(Instance instance, Adapter adapter) {
  struct RequestDeviceUserData {
    Device device;
    bool processFinished;
  };
  RequestDeviceUserData userData;

  auto onDeviceRequest = [](RequestDeviceStatus status, Device _device,
                            StringView message,
                            RequestDeviceUserData* userData) {
    userData->device = _device;
    SupportedLimits supportedLimits;
    _device.GetLimits(&supportedLimits);

    std::cout << supportedLimits.limits.maxBufferSize << std::endl;
  };

  SupportedLimits supportedLimits;
  adapter.GetLimits(&supportedLimits);

  RequiredLimits requiredLimits;

  requiredLimits.limits.maxVertexAttributes = 1;
  requiredLimits.limits.maxVertexBuffers = 1;
  requiredLimits.limits.maxBufferSize = 6 * 2 * sizeof(float);
  requiredLimits.limits.maxVertexBufferArrayStride = 2 * sizeof(float);

  requiredLimits.limits.minUniformBufferOffsetAlignment =
      supportedLimits.limits.minUniformBufferOffsetAlignment;
  requiredLimits.limits.minStorageBufferOffsetAlignment =
      supportedLimits.limits.minStorageBufferOffsetAlignment;

  DeviceDescriptor deviceDesc = {};
  deviceDesc.nextInChain = nullptr;
  deviceDesc.label = "My Device";
  deviceDesc.requiredFeatureCount = 0;
  deviceDesc.requiredLimits = nullptr;
  deviceDesc.defaultQueue.nextInChain = nullptr;
  deviceDesc.defaultQueue.label = "The default queue";
  deviceDesc.requiredLimits = &requiredLimits;

  std::cout << "required: " << requiredLimits.limits.maxBufferSize << std::endl;

  Future futureRequestDevice = adapter.RequestDevice(
      &deviceDesc, CallbackMode::WaitAnyOnly, onDeviceRequest, &userData);

  instance.WaitAny(futureRequestDevice, 0);

  return userData.device;
}

class Application {
 public:
  bool Initialize();
  void Terminate();
  void MainLoop();
  bool IsRunning();

 private:
  void InitializePipeline();
  TextureView GetNextSurfaceTextureView();
  void PlayingWithBuffers();
  RequiredLimits GetRequiredLimits(Adapter adapter) const;
  void InitializeBuffers();

 private:
  GLFWwindow* window;
  Device device;
  Queue queue;
  Surface surface;
  Instance instance;
  UncapturedErrorCallback<> uncapturedErrorCallbackHandle;
  TextureFormat surfaceFormat = TextureFormat::Undefined;
  RenderPipeline pipeline;
  Buffer vertexBuffer;
  uint32_t vertexCount;
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
  Adapter adapter = requestAdapterSync(instance, &adapterOpts);

  SupportedLimits supportedLimits;
  RequiredLimits requiredLimits = GetRequiredLimits(adapter);

  device = requestDeviceSync(instance, adapter);

  queue = device.GetQueue();
  SurfaceCapabilities surfaceCapabilities;
  surface.GetCapabilities(adapter, &surfaceCapabilities);
  surfaceFormat = surfaceCapabilities.formats[0];

  SurfaceConfiguration config = {};

  config.width = kWidth;
  config.height = kHeight;
  config.usage = TextureUsage::RenderAttachment;
  config.format = surfaceFormat;
  config.viewFormatCount = 0;
  config.viewFormats = nullptr;
  config.device = device;
  config.presentMode = PresentMode::Fifo;
  config.alphaMode = CompositeAlphaMode::Auto;

  surface.Configure(&config);

  std::cout << "Initialized" << " - surface " << surface.Get() << std::endl;

  InitializeBuffers();
  InitializePipeline();

  // PlayingWithBuffers();

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
  renderPass.SetVertexBuffer(0, vertexBuffer, 0, vertexBuffer.GetSize());
  // Draw 1 instance of a 3-vertices shape
  renderPass.Draw(vertexCount, 1, 0, 0);
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

  VertexBufferLayout vertexBufferLayout;
  VertexAttribute positionAttrib;

  positionAttrib.shaderLocation = 0;
  positionAttrib.format = VertexFormat::Float32x2;
  positionAttrib.offset = 0;

  vertexBufferLayout.attributeCount = 1;
  vertexBufferLayout.attributes = &positionAttrib;
  vertexBufferLayout.arrayStride = 2 * sizeof(float);
  vertexBufferLayout.stepMode = VertexStepMode::Vertex;

  pipelineDesc.vertex.bufferCount = 1;
  pipelineDesc.vertex.buffers = &vertexBufferLayout;
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

void Application::InitializeBuffers() {
  std::vector<float> vertexData = {// x0, y0
                                   -0.5, -0.5,

                                   // x1, y1
                                   +0.5, -0.5,

                                   // x2, y2
                                   +0.0, +0.5,

                                   // x0, y0
                                   -0.55f, -0.5,

                                   // x1, y1
                                   -0.05f, +0.5,

                                   // x2, y2
                                   -0.55f, +0.5};

  vertexCount = static_cast<uint32_t>(vertexData.size() / 2);

  BufferDescriptor bufferDesc;
  bufferDesc.size = vertexData.size() * sizeof(float);
  bufferDesc.usage =
      BufferUsage::CopyDst | BufferUsage::Vertex;  // Vertex usage here!
  bufferDesc.mappedAtCreation = false;
  vertexBuffer = device.CreateBuffer(&bufferDesc);

  // Upload geometry data to the buffer
  queue.WriteBuffer(vertexBuffer, 0, vertexData.data(), bufferDesc.size);
}

void Application::PlayingWithBuffers() {
  BufferDescriptor bufferDesc;
  bufferDesc.label = "Some GPU-side data buffer";
  bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::CopySrc;
  bufferDesc.size = 16;
  bufferDesc.mappedAtCreation = false;
  Buffer buffer1 = device.CreateBuffer(&bufferDesc);

  bufferDesc.label = "Output buffer";
  bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::MapRead;
  Buffer buffer2 = device.CreateBuffer(&bufferDesc);

  int bufferSize = 16;

  std::vector<uint8_t> numbers(bufferSize);
  for (uint8_t i = 0; i < bufferSize; ++i) numbers[i] = i;
  // Copy this from `numbers` (RAM) to `buffer1` (VRAM)
  queue.WriteBuffer(buffer1, 0, numbers.data(), numbers.size());

  CommandEncoder encoder = device.CreateCommandEncoder(nullptr);
  encoder.CopyBufferToBuffer(buffer1, 0, buffer2, 0, bufferSize);

  CommandBuffer command = encoder.Finish(nullptr);
  queue.Submit(1, &command);

  struct Context {
    Buffer buffer;
  };

  Context context = {buffer2};

  auto onBuffer2Mapped = [](MapAsyncStatus status, StringView message,
                            void* pUserData) {
    std::cout << "Buffer 2 mapped with status "
              << std::to_string(status == MapAsyncStatus::Success) << std::endl;

    if (status != MapAsyncStatus::Success) {
      return;
    }

    Context* context = reinterpret_cast<Context*>(pUserData);
    uint8_t* bufferData = (uint8_t*)context->buffer.GetConstMappedRange(0, 16);

    std::cout << "bufferData = [";
    for (int i = 0; i < 16; ++i) {
      if (i > 0) std::cout << ", ";
      std::cout << (int)bufferData[i];
    }
    std::cout << "]" << std::endl;

    context->buffer.Unmap();
  };

  Future futureMapping = buffer2.MapAsync(MapMode::Read, 0, numbers.size(),
                                          CallbackMode::WaitAnyOnly,
                                          onBuffer2Mapped, (void*)&context);

  instance.WaitAny(futureMapping, 0);
}

RequiredLimits Application::GetRequiredLimits(Adapter adapter) const {
  SupportedLimits supportedLimits;
  adapter.GetLimits(&supportedLimits);

  RequiredLimits requiredLimits;

  requiredLimits.limits.maxVertexAttributes = 1;
  requiredLimits.limits.maxVertexBuffers = 1;
  requiredLimits.limits.maxBufferSize = 6 * 2 * sizeof(float);
  requiredLimits.limits.maxVertexBufferArrayStride = 2 * sizeof(float);

  requiredLimits.limits.minUniformBufferOffsetAlignment =
      supportedLimits.limits.minUniformBufferOffsetAlignment;
  requiredLimits.limits.minStorageBufferOffsetAlignment =
      supportedLimits.limits.minStorageBufferOffsetAlignment;

  return requiredLimits;
}