#include "webgpu-utils.h"
#include <iostream>
#include <cassert>
#include <vector>

WGPUInstance createInstance()
{
  WGPUInstanceDescriptor desc = {};
  desc.nextInChain = nullptr;

#ifdef WEBGPU_BACKEND_EMSCRIPTEN
  WGPUInstance instance = wgpuCreateInstance(nullptr);
#else  //  WEBGPU_BACKEND_EMSCRIPTEN
  WGPUInstance instance = wgpuCreateInstance(&desc);
#endif //  WEBGPU_BACKEND_EMSCRIPTEN

  return instance;
}

WGPUAdapter requestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const *options)
{
  struct UserData
  {
    WGPUAdapter adapter = nullptr;
    bool requestEnded = false;
  };
  UserData userData;

  auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, struct WGPUStringView message, void *pUserData, void *pUserData2)
  {
    UserData &userData = *reinterpret_cast<UserData *>(pUserData);
    if (status == WGPURequestAdapterStatus_Success)
    {
      userData.adapter = adapter;
    }
    else
    {
      std::cout << "Could not get WebGPU adapter: " << (message.data ? message.data : "Unknown error") << std::endl;
    }
    userData.requestEnded = true;
  };

  WGPURequestAdapterCallbackInfo callbackInfo = {
      .nextInChain = nullptr,
      .mode = WGPUCallbackMode_AllowSpontaneous,
      .callback = onAdapterRequestEnded,
      .userdata1 = (void *)&userData,
  };

  // Call to the WebGPU request adapter procedure
  wgpuInstanceRequestAdapter(instance, options, callbackInfo);

  // We wait until userData.requestEnded gets true
  assert(userData.requestEnded);

  return userData.adapter;
}

WGPUSupportedFeatures getAdapterSupportedFeatures(WGPUAdapter adapter)
{
  std::vector<WGPUFeatureName> features(32);

  WGPUSupportedFeatures supportedFeatures = {
      .featureCount = features.size(),
      .features = features.data(),
  };

  wgpuAdapterGetFeatures(adapter, &supportedFeatures);

  return supportedFeatures;
}

bool getAdapterSupportedLimits(WGPUAdapter adapter, WGPUSupportedLimits *supportedLimits)
{
#ifdef WEBGPU_BACKEND_DAWN
  bool success = wgpuAdapterGetLimits(adapter, &supportedLimits) == WGPUStatus_Success;
#else
  bool success = wgpuAdapterGetLimits(adapter, supportedLimits);
#endif
  return success;
}

WGPUAdapterInfo getAdapterInfo(WGPUAdapter adapter)
{
#ifndef __EMSCRIPTEN__
  WGPUAdapterInfo info = {};
  bool successInfo = wgpuAdapterGetInfo(adapter, &info);
  return info;
#endif
}

WGPUDevice requestDeviceSync(WGPUAdapter adapter, WGPUDeviceDescriptor *descriptor)
{
  struct UserData
  {
    WGPUDevice device = nullptr;
    bool requestEnded = false;
  };

  UserData userData;

  descriptor->deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
  descriptor->deviceLostCallbackInfo.callback = [](WGPUDevice const *device, WGPUDeviceLostReason reason, struct WGPUStringView message, void *userdata1, void *userdata2)
  {
    std::cout << "Device lost: reason " << reason;
    if (message.data)
      std::cout << " (" << message.data << ")";
    std::cout << std::endl;
  };

  auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, struct WGPUStringView message, void *pUserData, void *pUserData2)
  {
    UserData &userData = *reinterpret_cast<UserData *>(pUserData);
    if (status == WGPURequestDeviceStatus_Success)
    {
      userData.device = device;
    }
    else
    {
      std::cout << "Could not get WebGPU device: " << message.data << std::endl;
    }
    userData.requestEnded = true;
  };

  WGPURequestDeviceCallbackInfo callbackInfo = {
      .nextInChain = nullptr,
      .mode = WGPUCallbackMode_AllowSpontaneous,
      .callback = onDeviceRequestEnded,
      .userdata1 = (void *)&userData,
  };

  wgpuAdapterRequestDevice(adapter, descriptor, callbackInfo);

  assert(userData.requestEnded);

  return userData.device;
}

bool getDeviceSupportedLimits(WGPUDevice device, WGPUSupportedLimits *supportedLimits)
{
#ifdef WEBGPU_BACKEND_DAWN
  bool success = wgpuDeviceGetLimits(device, supportedLimits) == WGPUStatus_Success;
#else
  bool success = wgpuDeviceGetLimits(device, supportedLimits);
#endif

  return success;
}

WGPUSupportedFeatures getDeviceSupportedFeatures(WGPUDevice device)
{
  std::vector<WGPUFeatureName> features(32);

  WGPUSupportedFeatures supportedFeatures = {
      .featureCount = features.size(),
      .features = features.data(),
  };

  wgpuDeviceGetFeatures(device, &supportedFeatures);

  return supportedFeatures;
}
