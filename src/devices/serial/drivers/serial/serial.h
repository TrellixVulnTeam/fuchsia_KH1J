// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SERIAL_DRIVERS_SERIAL_SERIAL_H_
#define SRC_DEVICES_SERIAL_DRIVERS_SERIAL_SERIAL_H_

#include <fidl/fuchsia.hardware.serial/cpp/wire.h>
#include <fuchsia/hardware/serial/cpp/banjo.h>
#include <fuchsia/hardware/serialimpl/cpp/banjo.h>
#include <lib/ddk/driver.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/event.h>
#include <lib/zx/socket.h>
#include <zircon/types.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/mutex.h>

namespace serial {

class SerialDevice;
using DeviceType = ddk::Device<SerialDevice, ddk::Openable, ddk::Closable,
                               ddk::Messageable<fuchsia_hardware_serial::Device>::Mixin>;

class SerialDevice : public DeviceType,
                     public ddk::SerialProtocol<SerialDevice, ddk::base_protocol> {
 public:
  explicit SerialDevice(zx_device_t* parent) : DeviceType(parent), serial_(parent), open_(false) {}

  static zx_status_t Create(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  zx_status_t Init();

  // Device protocol implementation.
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  zx_status_t DdkClose(uint32_t flags);
  void DdkRelease();

  // Serial protocol implementation.
  zx_status_t SerialGetInfo(serial_port_info_t* info);
  zx_status_t SerialConfig(uint32_t baud_rate, uint32_t flags);
  void Read(ReadCompleter::Sync& completer) override;
  void Write(WriteRequestView request, WriteCompleter::Sync& completer) override;
  zx_status_t SerialOpenSocket(zx::socket* out_handle);

 private:
  zx_status_t WorkerThread();
  void StateCallback(serial_state_t state);

  // Fidl protocol implementation.
  void GetClass(GetClassCompleter::Sync& completer) override;
  void SetConfig(SetConfigRequestView request, SetConfigCompleter::Sync& completer) override;

  // The serial protocol of the device we are binding against.
  ddk::SerialImplProtocolClient serial_;

  zx::socket socket_;  // socket used for communicating with our client
  zx::event event_;    // event for signaling serial driver state changes

  fbl::Mutex lock_;
  thrd_t thread_;
  uint32_t serial_class_;
  bool open_ TA_GUARDED(lock_);
};

}  // namespace serial

#endif  // SRC_DEVICES_SERIAL_DRIVERS_SERIAL_SERIAL_H_
