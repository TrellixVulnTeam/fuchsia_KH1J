// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spi-child.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/trace/event.h>
#include <lib/trace-engine/types.h>

#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>
#include <fbl/vector.h>

#include "spi.h"

namespace spi {

namespace sharedmemory = fuchsia_hardware_sharedmemory;

void SpiChild::TransmitVector(TransmitVectorRequestView request,
                              TransmitVectorCompleter::Sync& completer) {
  if (shutdown_) {
    completer.Reply(ZX_ERR_CANCELED);
    return;
  }

  size_t rx_actual;
  zx_status_t status =
      spi_.Exchange(cs_, request->data.data(), request->data.count(), nullptr, 0, &rx_actual);
  if (status == ZX_OK) {
    completer.Reply(ZX_OK);
  } else {
    completer.Reply(status);
  }
}

void SpiChild::ReceiveVector(ReceiveVectorRequestView request,
                             ReceiveVectorCompleter::Sync& completer) {
  if (shutdown_) {
    completer.Reply(ZX_ERR_CANCELED, fidl::VectorView<uint8_t>());
    return;
  }

  fbl::Vector<uint8_t> rxdata;
  rxdata.reserve(request->size);
  size_t rx_actual;
  zx_status_t status = spi_.Exchange(cs_, nullptr, 0, rxdata.begin(), request->size, &rx_actual);
  if (status == ZX_OK && rx_actual == request->size) {
    auto rx_vector = fidl::VectorView<uint8_t>::FromExternal(rxdata.data(), request->size);
    completer.Reply(ZX_OK, std::move(rx_vector));
  } else {
    completer.Reply(status == ZX_OK ? ZX_ERR_INTERNAL : status, fidl::VectorView<uint8_t>());
  }
}

void SpiChild::ExchangeVector(ExchangeVectorRequestView request,
                              ExchangeVectorCompleter::Sync& completer) {
  if (shutdown_) {
    completer.Reply(ZX_ERR_CANCELED, fidl::VectorView<uint8_t>());
    return;
  }

  fbl::Vector<uint8_t> rxdata;
  const size_t size = request->txdata.count();
  rxdata.reserve(size);
  size_t rx_actual;
  zx_status_t status =
      spi_.Exchange(cs_, request->txdata.data(), size, rxdata.begin(), size, &rx_actual);
  if (status == ZX_OK && rx_actual == size) {
    auto rx_vector = fidl::VectorView<uint8_t>::FromExternal(rxdata.data(), size);
    completer.Reply(ZX_OK, std::move(rx_vector));
  } else {
    completer.Reply(status == ZX_OK ? ZX_ERR_INTERNAL : status, fidl::VectorView<uint8_t>());
  }
}

void SpiChild::RegisterVmo(RegisterVmoRequestView request, RegisterVmoCompleter::Sync& completer) {
  zx_status_t status =
      spi_.RegisterVmo(cs_, request->vmo_id, std::move(request->vmo.vmo), request->vmo.offset,
                       request->vmo.size, static_cast<uint32_t>(request->rights));
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void SpiChild::UnregisterVmo(UnregisterVmoRequestView request,
                             UnregisterVmoCompleter::Sync& completer) {
  zx::vmo vmo;
  zx_status_t status = spi_.UnregisterVmo(cs_, request->vmo_id, &vmo);
  if (status == ZX_OK) {
    completer.ReplySuccess(std::move(vmo));
  } else {
    completer.ReplyError(status);
  }
}

void SpiChild::Transmit(TransmitRequestView request, TransmitCompleter::Sync& completer) {
  zx_status_t status;
  if (shutdown_) {
    status = ZX_ERR_CANCELED;
  } else {
    TRACE_DURATION("spi", "Transmit", "cs", cs_, "size", request->buffer.size);
    status =
        spi_.TransmitVmo(cs_, request->buffer.vmo_id, request->buffer.offset, request->buffer.size);
  }

  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void SpiChild::Receive(ReceiveRequestView request, ReceiveCompleter::Sync& completer) {
  zx_status_t status;
  if (shutdown_) {
    status = ZX_ERR_CANCELED;
  } else {
    TRACE_DURATION("spi", "Receive", "cs", cs_, "size", request->buffer.size);
    status =
        spi_.ReceiveVmo(cs_, request->buffer.vmo_id, request->buffer.offset, request->buffer.size);
  }

  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void SpiChild::Exchange(ExchangeRequestView request, ExchangeCompleter::Sync& completer) {
  zx_status_t status;
  if (shutdown_) {
    status = ZX_ERR_CANCELED;
  } else if (request->tx_buffer.size != request->rx_buffer.size) {
    status = ZX_ERR_INVALID_ARGS;
  } else {
    TRACE_DURATION("spi", "Exchange", "cs", cs_, "size", request->tx_buffer.size);
    status = spi_.ExchangeVmo(cs_, request->tx_buffer.vmo_id, request->tx_buffer.offset,
                              request->rx_buffer.vmo_id, request->rx_buffer.offset,
                              request->tx_buffer.size);
  }

  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

zx_status_t SpiChild::SpiTransmit(const uint8_t* txdata_list, size_t txdata_count) {
  if (shutdown_) {
    return ZX_ERR_CANCELED;
  }

  size_t actual;
  spi_.Exchange(cs_, txdata_list, txdata_count, nullptr, 0, &actual);
  return ZX_OK;
}
zx_status_t SpiChild::SpiReceive(uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count,
                                 size_t* out_rxdata_actual) {
  if (shutdown_) {
    return ZX_ERR_CANCELED;
  }

  spi_.Exchange(cs_, nullptr, 0, out_rxdata_list, rxdata_count, out_rxdata_actual);
  return ZX_OK;
}

zx_status_t SpiChild::SpiExchange(const uint8_t* txdata_list, size_t txdata_count,
                                  uint8_t* out_rxdata_list, size_t rxdata_count,
                                  size_t* out_rxdata_actual) {
  if (shutdown_) {
    return ZX_ERR_CANCELED;
  }

  spi_.Exchange(cs_, txdata_list, txdata_count, out_rxdata_list, rxdata_count, out_rxdata_actual);
  return ZX_OK;
}

void SpiChild::CanAssertCs(CanAssertCsCompleter::Sync& completer) {
  completer.Reply(!has_siblings_);
}

void SpiChild::AssertCs(AssertCsCompleter::Sync& completer) {
  if (shutdown_ || has_siblings_) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  } else {
    completer.Reply(spi_.LockBus(cs_));
  }
}

void SpiChild::DeassertCs(DeassertCsCompleter::Sync& completer) {
  if (shutdown_ || has_siblings_) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  } else {
    completer.Reply(spi_.UnlockBus(cs_));
  }
}

void SpiChild::SpiConnectServer(zx::channel server) {
  fbl::AutoLock lock(&lock_);
  if (!shutdown_ && !connected_) {
    spi_parent_.ConnectServer(std::move(server), fbl::RefPtr(this));
    connected_ = true;
  } else {
    fidl::ServerEnd<fuchsia_hardware_spi::Device>(std::move(server)).Close(ZX_ERR_ALREADY_BOUND);
  }
}

zx_status_t SpiChild::Open() {
  fbl::AutoLock lock(&lock_);
  if (connected_) {
    return ZX_ERR_ALREADY_BOUND;
  }
  connected_ = true;
  return ZX_OK;
}

zx_status_t SpiChild::Close() {
  fbl::AutoLock lock(&lock_);
  spi_.ReleaseRegisteredVmos(cs_);
  connected_ = false;
  return ZX_OK;
}

void SpiChild::DdkUnbind(ddk::UnbindTxn txn) {
  shutdown_ = true;
  txn.Reply();
}

void SpiChild::DdkRelease() {
  // The DDK is releasing its reference to this object, so create a RefPtr to it without
  // incrementing the counter. When it goes out of scope this object will be freed if needed.
  fbl::RefPtr<SpiChild> thiz = fbl::ImportFromRawPtr(this);
}

void SpiFidlChild::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void SpiFidlChild::TransmitVector(TransmitVectorRequestView request,
                                  TransmitVectorCompleter::Sync& completer) {
  spi_->TransmitVector(request, completer);
}

void SpiFidlChild::ReceiveVector(ReceiveVectorRequestView request,
                                 ReceiveVectorCompleter::Sync& completer) {
  spi_->ReceiveVector(request, completer);
}

void SpiFidlChild::ExchangeVector(ExchangeVectorRequestView request,
                                  ExchangeVectorCompleter::Sync& completer) {
  spi_->ExchangeVector(request, completer);
}

void SpiFidlChild::RegisterVmo(RegisterVmoRequestView request,
                               RegisterVmoCompleter::Sync& completer) {
  spi_->RegisterVmo(request, completer);
}

void SpiFidlChild::UnregisterVmo(UnregisterVmoRequestView request,
                                 UnregisterVmoCompleter::Sync& completer) {
  spi_->UnregisterVmo(request, completer);
}

void SpiFidlChild::Transmit(TransmitRequestView request, TransmitCompleter::Sync& completer) {
  spi_->Transmit(request, completer);
}

void SpiFidlChild::Receive(ReceiveRequestView request, ReceiveCompleter::Sync& completer) {
  spi_->Receive(request, completer);
}

void SpiFidlChild::Exchange(ExchangeRequestView request, ExchangeCompleter::Sync& completer) {
  spi_->Exchange(request, completer);
}

void SpiFidlChild::CanAssertCs(CanAssertCsCompleter::Sync& completer) {
  spi_->CanAssertCs(completer);
}

void SpiFidlChild::AssertCs(AssertCsCompleter::Sync& completer) { spi_->AssertCs(completer); }

void SpiFidlChild::DeassertCs(DeassertCsCompleter::Sync& completer) { spi_->DeassertCs(completer); }

zx_status_t SpiFidlChild::SetUpOutgoingDirectory(
    fidl::ServerEnd<fuchsia_io::Directory> server_end) {
  zx::result status =
      outgoing_.AddProtocol(this, fidl::DiscoverableProtocolName<fuchsia_hardware_spi::Device>);
  if (status.is_error()) {
    return status.status_value();
  }

  return outgoing_.Serve(std::move(server_end)).status_value();
}

void SpiBanjoChild::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

zx_status_t SpiBanjoChild::DdkOpen(zx_device_t** dev_out, uint32_t flags) { return spi_->Open(); }

zx_status_t SpiBanjoChild::DdkClose(uint32_t flags) { return spi_->Close(); }

zx_status_t SpiBanjoChild::SpiTransmit(const uint8_t* txdata_list, size_t txdata_count) {
  return spi_->SpiTransmit(txdata_list, txdata_count);
}

zx_status_t SpiBanjoChild::SpiReceive(uint32_t size, uint8_t* out_rxdata_list, size_t rxdata_count,
                                      size_t* out_rxdata_actual) {
  return spi_->SpiReceive(size, out_rxdata_list, rxdata_count, out_rxdata_actual);
}

zx_status_t SpiBanjoChild::SpiExchange(const uint8_t* txdata_list, size_t txdata_count,
                                       uint8_t* out_rxdata_list, size_t rxdata_count,
                                       size_t* out_rxdata_actual) {
  return spi_->SpiExchange(txdata_list, txdata_count, out_rxdata_list, rxdata_count,
                           out_rxdata_actual);
}

void SpiBanjoChild::SpiConnectServer(zx::channel server) {
  spi_->SpiConnectServer(std::move(server));
}

void SpiBanjoChild::TransmitVector(TransmitVectorRequestView request,
                                   TransmitVectorCompleter::Sync& completer) {
  spi_->TransmitVector(request, completer);
}

void SpiBanjoChild::ReceiveVector(ReceiveVectorRequestView request,
                                  ReceiveVectorCompleter::Sync& completer) {
  spi_->ReceiveVector(request, completer);
}

void SpiBanjoChild::ExchangeVector(ExchangeVectorRequestView request,
                                   ExchangeVectorCompleter::Sync& completer) {
  spi_->ExchangeVector(request, completer);
}

void SpiBanjoChild::RegisterVmo(RegisterVmoRequestView request,
                                RegisterVmoCompleter::Sync& completer) {
  spi_->RegisterVmo(request, completer);
}

void SpiBanjoChild::UnregisterVmo(UnregisterVmoRequestView request,
                                  UnregisterVmoCompleter::Sync& completer) {
  spi_->UnregisterVmo(request, completer);
}

void SpiBanjoChild::Transmit(TransmitRequestView request, TransmitCompleter::Sync& completer) {
  spi_->Transmit(request, completer);
}

void SpiBanjoChild::Receive(ReceiveRequestView request, ReceiveCompleter::Sync& completer) {
  spi_->Receive(request, completer);
}

void SpiBanjoChild::Exchange(ExchangeRequestView request, ExchangeCompleter::Sync& completer) {
  spi_->Exchange(request, completer);
}

void SpiBanjoChild::CanAssertCs(CanAssertCsCompleter::Sync& completer) {
  spi_->CanAssertCs(completer);
}

void SpiBanjoChild::AssertCs(AssertCsCompleter::Sync& completer) { spi_->AssertCs(completer); }

void SpiBanjoChild::DeassertCs(DeassertCsCompleter::Sync& completer) {
  spi_->DeassertCs(completer);
}

}  // namespace spi
