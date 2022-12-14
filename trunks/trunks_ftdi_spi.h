// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TRUNKS_FTDI_SPI_H_
#define TRUNKS_TRUNKS_FTDI_SPI_H_

#include <string>

#include "trunks/command_transceiver.h"
#include "trunks/trunks_export.h"

#if defined SPI_OVER_FTDI

#include "trunks/ftdi/mpsse.h"

namespace trunks {

// TrunksFtdiSpi is a CommandTransceiver implementation that forwards all
// commands to the SPI over FTDI interface directly to a TPM chip.
class TRUNKS_EXPORT TrunksFtdiSpi : public CommandTransceiver {
 public:
  TrunksFtdiSpi() : mpsse_(NULL), locality_(0) {}
  TrunksFtdiSpi(const TrunksFtdiSpi&) = delete;
  TrunksFtdiSpi& operator=(const TrunksFtdiSpi&) = delete;

  ~TrunksFtdiSpi() override;

  // CommandTransceiver methods.
  bool Init() override;
  void SendCommand(const std::string& command,
                   const ResponseCallback& callback) override;
  std::string SendCommandAndWait(const std::string& command) override;

 private:
  struct mpsse_context* mpsse_;
  unsigned locality_;  // Set at initialization.

  // Read a TPM register into the passed in buffer, where 'bytes' the width of
  // the register. Return true on success, false on failure.
  bool FtdiReadReg(unsigned reg_number, size_t bytes, void* buffer);
  // Write a TPM register from the passed in buffer, where 'bytes' the width of
  // the register. Return true on success, false on failure.
  bool FtdiWriteReg(unsigned reg_number, size_t bytes, const void* buffer);
  // Generate a proper SPI frame for read/write transaction, read_write set to
  // true for read transactions, the size of the transaction is passed as
  // 'bytes', addr is the internal TPM address space address (accounting for
  // locality).
  //
  // Note that this function is expected to be called when the SPI bus is idle
  // (CS deasserted), and will assert the CS before transmitting.
  void StartTransaction(bool read_write, size_t bytes, unsigned addr);
  // TPM Status Register is going to be accessed a lot, let's have dedicated
  // accessors for it,
  bool ReadTpmSts(uint32_t* status);
  bool WriteTpmSts(uint32_t status);
  // Poll status register until the required value is read or the timeout
  // expires.
  bool WaitForStatus(uint32_t statusMask,
                     uint32_t statusExpected,
                     int timeout_ms = 10000);
  // Retrieve current value of the burst count field.
  size_t GetBurstCount(void);
};

}  // namespace trunks

#else  // SPI_OVER_FTDI ^^^^ defined  vvvvv NOT defined

namespace trunks {

// A plug to support compilations on platforms where FTDI SPI interface is not
// available.
class TRUNKS_EXPORT TrunksFtdiSpi : public CommandTransceiver {
 public:
  TrunksFtdiSpi() {}
  ~TrunksFtdiSpi() {}

  bool Init() { return false; }
  void SendCommand(const std::string& command, ResponseCallback callback) {}
  std::string SendCommandAndWait(const std::string& command) {
    return std::string("");
  }
};

}  // namespace trunks

#endif  // SPI_OVER_FTDI ^^^^ NOT defined

#endif  // TRUNKS_TRUNKS_FTDI_SPI_H_
