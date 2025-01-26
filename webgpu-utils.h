#pragma once

#include <webgpu/webgpu.h>

WGPUInstance createInstance();

WGPUAdapter requestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const *options);

WGPUSupportedFeatures getAdapterSupportedFeatures(WGPUAdapter adapter);

bool getAdapterSupportedLimits(WGPUAdapter adapter, WGPUSupportedLimits *supportedLimits);

WGPUAdapterInfo getAdapterInfo(WGPUAdapter adapter);

WGPUDevice requestDeviceSync(WGPUAdapter adapter, WGPUDeviceDescriptor *descriptor);

bool getDeviceSupportedLimits(WGPUDevice device, WGPUSupportedLimits *supportedLimits);

WGPUSupportedFeatures getDeviceSupportedFeatures(WGPUDevice device);