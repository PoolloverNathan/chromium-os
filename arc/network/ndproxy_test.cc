// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/ndproxy.h"

#include <stdlib.h>

#include <net/ethernet.h>

#include <gtest/gtest.h>

namespace arc_networkd {

const uint8_t physical_if_mac[] = "\xa0\xce\xc8\xc6\x91\x0a";
const uint8_t guest_if_mac[] = "\xd2\x47\xf7\xc5\x9e\x53";

const uint8_t ping_frame[] =
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x86\xdd\x60\x0b"
    "\x8d\xb4\x00\x40\x3a\x40\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x01\x80\x00\xb9\x3c\x13\x8f\x00\x09\xde\x6a"
    "\x78\x5d\x00\x00\x00\x00\x8e\x13\x0f\x00\x00\x00\x00\x00\x10\x11"
    "\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x20\x21"
    "\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30\x31"
    "\x32\x33\x34\x35\x36\x37";

const uint8_t rs_frame[] =
    "\x33\x33\x00\x00\x00\x02\x1a\x9b\x82\xbd\xc0\xa0\x86\xdd\x60\x00"
    "\x00\x00\x00\x10\x3a\xff\xfe\x80\x00\x00\x00\x00\x00\x00\x2d\x75"
    "\xb2\x80\x97\x83\x76\xbf\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x02\x85\x00\x2f\xfc\x00\x00\x00\x00\x01\x01"
    "\x1a\x9b\x82\xbd\xc0\xa0";

const uint8_t rs_frame_translated[] =
    "\x33\x33\x00\x00\x00\x02\xa0\xce\xc8\xc6\x91\x0a\x86\xdd\x60\x00"
    "\x00\x00\x00\x10\x3a\xff\xfe\x80\x00\x00\x00\x00\x00\x00\x2d\x75"
    "\xb2\x80\x97\x83\x76\xbf\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x02\x85\x00\x93\x55\x00\x00\x00\x00\x01\x01"
    "\xa0\xce\xc8\xc6\x91\x0a";

const uint8_t ra_frame[] =
    "\x33\x33\x00\x00\x00\x01\xc4\x71\xfe\xf1\xf6\x7f\x86\xdd\x6e\x00"
    "\x00\x00\x00\x40\x3a\xff\xfe\x80\x00\x00\x00\x00\x00\x00\xc6\x71"
    "\xfe\xff\xfe\xf1\xf6\x7f\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x01\x86\x00\x8a\xd5\x40\x00\x07\x08\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x01\x01\xc4\x71\xfe\xf1\xf6\x7f\x05\x01"
    "\x00\x00\x00\x00\x05\xdc\x03\x04\x40\xc0\x00\x27\x8d\x00\x00\x09"
    "\x3a\x80\x00\x00\x00\x00\x24\x01\xfa\x00\x00\x04\x00\x02\x00\x00"
    "\x00\x00\x00\x00\x00\x00";

const uint8_t ra_frame_translated[] =
    "\x33\x33\x00\x00\x00\x01\xd2\x47\xf7\xc5\x9e\x53\x86\xdd\x6e\x00"
    "\x00\x00\x00\x40\x3a\xff\xfe\x80\x00\x00\x00\x00\x00\x00\xc6\x71"
    "\xfe\xff\xfe\xf1\xf6\x7f\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x01\x86\x00\xdc\x53\x40\x04\x07\x08\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x01\x01\xd2\x47\xf7\xc5\x9e\x53\x05\x01"
    "\x00\x00\x00\x00\x05\xdc\x03\x04\x40\xc0\x00\x27\x8d\x00\x00\x09"
    "\x3a\x80\x00\x00\x00\x00\x24\x01\xfa\x00\x00\x04\x00\x02\x00\x00"
    "\x00\x00\x00\x00\x00\x00";

const uint8_t ra_frame_option_reordered[] =
    "\x33\x33\x00\x00\x00\x01\xc4\x71\xfe\xf1\xf6\x7f\x86\xdd\x6e\x00"
    "\x00\x00\x00\x40\x3a\xff\xfe\x80\x00\x00\x00\x00\x00\x00\xc6\x71"
    "\xfe\xff\xfe\xf1\xf6\x7f\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x01\x86\x00\x8a\xd5\x40\x00\x07\x08\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x05\x01\x00\x00\x00\x00\x05\xdc\x01\x01"
    "\xc4\x71\xfe\xf1\xf6\x7f\x03\x04\x40\xc0\x00\x27\x8d\x00\x00\x09"
    "\x3a\x80\x00\x00\x00\x00\x24\x01\xfa\x00\x00\x04\x00\x02\x00\x00"
    "\x00\x00\x00\x00\x00\x00";

const uint8_t ra_frame_option_reordered_translated[] =
    "\x33\x33\x00\x00\x00\x01\xd2\x47\xf7\xc5\x9e\x53\x86\xdd\x6e\x00"
    "\x00\x00\x00\x40\x3a\xff\xfe\x80\x00\x00\x00\x00\x00\x00\xc6\x71"
    "\xfe\xff\xfe\xf1\xf6\x7f\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x01\x86\x00\xdc\x53\x40\x04\x07\x08\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x05\x01\x00\x00\x00\x00\x05\xdc\x01\x01"
    "\xd2\x47\xf7\xc5\x9e\x53\x03\x04\x40\xc0\x00\x27\x8d\x00\x00\x09"
    "\x3a\x80\x00\x00\x00\x00\x24\x01\xfa\x00\x00\x04\x00\x02\x00\x00"
    "\x00\x00\x00\x00\x00\x00";

const uint8_t ns_frame[] =
    "\xd2\x47\xf7\xc5\x9e\x53\x1a\x9b\x82\xbd\xc0\xa0\x86\xdd\x60\x00"
    "\x00\x00\x00\x20\x3a\xff\xfe\x80\x00\x00\x00\x00\x00\x00\x2d\x75"
    "\xb2\x80\x97\x83\x76\xbf\xfe\x80\x00\x00\x00\x00\x00\x00\xc6\x71"
    "\xfe\xff\xfe\xf1\xf6\x7f\x87\x00\xba\x27\x00\x00\x00\x00\xfe\x80"
    "\x00\x00\x00\x00\x00\x00\xc6\x71\xfe\xff\xfe\xf1\xf6\x7f\x01\x01"
    "\x1a\x9b\x82\xbd\xc0\xa0";

const uint8_t ns_frame_translated[] =
    "\xff\xff\xff\xff\xff\xff\xa0\xce\xc8\xc6\x91\x0a\x86\xdd\x60\x00"
    "\x00\x00\x00\x20\x3a\xff\xfe\x80\x00\x00\x00\x00\x00\x00\x2d\x75"
    "\xb2\x80\x97\x83\x76\xbf\xfe\x80\x00\x00\x00\x00\x00\x00\xc6\x71"
    "\xfe\xff\xfe\xf1\xf6\x7f\x87\x00\x1d\x81\x00\x00\x00\x00\xfe\x80"
    "\x00\x00\x00\x00\x00\x00\xc6\x71\xfe\xff\xfe\xf1\xf6\x7f\x01\x01"
    "\xa0\xce\xc8\xc6\x91\x0a";

const uint8_t na_frame[] =
    "\xa0\xce\xc8\xc6\x91\x0a\xc4\x71\xfe\xf1\xf6\x7f\x86\xdd\x6e\x00"
    "\x00\x00\x00\x18\x3a\xff\xfe\x80\x00\x00\x00\x00\x00\x00\xc6\x71"
    "\xfe\xff\xfe\xf1\xf6\x7f\xfe\x80\x00\x00\x00\x00\x00\x00\x2d\x75"
    "\xb2\x80\x97\x83\x76\xbf\x88\x00\x58\x29\xc0\x00\x00\x00\xfe\x80"
    "\x00\x00\x00\x00\x00\x00\xc6\x71\xfe\xff\xfe\xf1\xf6\x7f";

const uint8_t na_frame_translated[] =
    "\xff\xff\xff\xff\xff\xff\xd2\x47\xf7\xc5\x9e\x53\x86\xdd\x6e\x00"
    "\x00\x00\x00\x18\x3a\xff\xfe\x80\x00\x00\x00\x00\x00\x00\xc6\x71"
    "\xfe\xff\xfe\xf1\xf6\x7f\xfe\x80\x00\x00\x00\x00\x00\x00\x2d\x75"
    "\xb2\x80\x97\x83\x76\xbf\x88\x00\x58\x29\xc0\x00\x00\x00\xfe\x80"
    "\x00\x00\x00\x00\x00\x00\xc6\x71\xfe\xff\xfe\xf1\xf6\x7f";

const uint8_t tcp_frame[] =
    "\xc4\x71\xfe\xf1\xf6\x7f\xa0\xce\xc8\xc6\x91\x0a\x86\xdd\x60\x03"
    "\xa3\x57\x00\x20\x06\x40\x24\x01\xfa\x00\x00\x04\x00\x02\xf0\x94"
    "\x0d\xa1\x12\x6f\xfd\x6b\x24\x04\x68\x00\x40\x08\x0c\x07\x00\x00"
    "\x00\x00\x00\x00\x00\x66\x85\xc0\x01\xbb\xb2\x7e\xd0\xa6\x0c\x57"
    "\xa5\x6c\x80\x10\x01\x54\x04\xb9\x00\x00\x01\x01\x08\x0a\x00\x5a"
    "\x59\xc0\x32\x53\x14\x3a";

TEST(NDProxy, Icmpv6Checksum) {
  uint8_t buffer_extended[IP_MAXPACKET + ETHER_HDR_LEN + 4];
  uint8_t* buffer = NDProxy::AlignFrameBuffer(buffer_extended);

  ip6_hdr* ip6 = reinterpret_cast<ip6_hdr*>(buffer + ETHER_HDR_LEN);
  icmp6_hdr* icmp6 =
      reinterpret_cast<icmp6_hdr*>(buffer + ETHER_HDR_LEN + sizeof(ip6_hdr));

  memcpy(buffer, ping_frame, sizeof(ping_frame));
  uint16_t ori_cksum = icmp6->icmp6_cksum;
  icmp6->icmp6_cksum = 0;
  EXPECT_EQ(ori_cksum, NDProxy::Icmpv6Checksum(ip6, icmp6));

  memcpy(buffer, rs_frame, sizeof(rs_frame));
  ori_cksum = icmp6->icmp6_cksum;
  icmp6->icmp6_cksum = 0;
  EXPECT_EQ(ori_cksum, NDProxy::Icmpv6Checksum(ip6, icmp6));
}

TEST(NDProxy, TranslateFrame) {
  uint8_t in_buffer_extended[IP_MAXPACKET + ETHER_HDR_LEN + 4];
  uint8_t out_buffer_extended[IP_MAXPACKET + ETHER_HDR_LEN + 4];
  uint8_t* in_buffer = NDProxy::AlignFrameBuffer(in_buffer_extended);
  uint8_t* out_buffer = NDProxy::AlignFrameBuffer(out_buffer_extended);
  int result;

  memcpy(in_buffer, tcp_frame, sizeof(tcp_frame));
  result = NDProxy::TranslateNDFrame(in_buffer, sizeof(tcp_frame),
                                     physical_if_mac, out_buffer);
  EXPECT_EQ(NDProxy::kTranslateErrorNotICMPv6Frame, result);

  memcpy(in_buffer, ping_frame, sizeof(ping_frame));
  result = NDProxy::TranslateNDFrame(in_buffer, sizeof(ping_frame),
                                     physical_if_mac, out_buffer);
  EXPECT_EQ(NDProxy::kTranslateErrorNotNDFrame, result);

  memcpy(in_buffer, rs_frame, sizeof(rs_frame));
  result = NDProxy::TranslateNDFrame(in_buffer, sizeof(rs_frame),
                                     physical_if_mac, out_buffer);
  EXPECT_EQ(sizeof(rs_frame_translated), result);
  EXPECT_EQ(0, memcmp(rs_frame_translated, out_buffer, sizeof(rs_frame)));

  memcpy(in_buffer, ra_frame, sizeof(ra_frame));
  result = NDProxy::TranslateNDFrame(in_buffer, sizeof(ra_frame), guest_if_mac,
                                     out_buffer);
  EXPECT_EQ(sizeof(ra_frame_translated), result);
  EXPECT_EQ(0, memcmp(ra_frame_translated, out_buffer, sizeof(ra_frame)));

  memcpy(in_buffer, ra_frame_option_reordered,
         sizeof(ra_frame_option_reordered));
  result = NDProxy::TranslateNDFrame(
      in_buffer, sizeof(ra_frame_option_reordered), guest_if_mac, out_buffer);
  EXPECT_EQ(sizeof(ra_frame_option_reordered_translated), result);
  EXPECT_EQ(0, memcmp(ra_frame_option_reordered_translated, out_buffer,
                      sizeof(ra_frame_option_reordered)));

  memcpy(in_buffer, ns_frame, sizeof(ns_frame));
  result = NDProxy::TranslateNDFrame(in_buffer, sizeof(ns_frame),
                                     physical_if_mac, out_buffer);
  EXPECT_EQ(sizeof(ns_frame_translated), result);
  EXPECT_EQ(0, memcmp(ns_frame_translated, out_buffer, sizeof(ns_frame)));

  memcpy(in_buffer, na_frame, sizeof(na_frame));
  result = NDProxy::TranslateNDFrame(in_buffer, sizeof(na_frame), guest_if_mac,
                                     out_buffer);
  EXPECT_EQ(sizeof(na_frame_translated), result);
  EXPECT_EQ(0, memcmp(na_frame_translated, out_buffer, sizeof(na_frame)));
}

}  // namespace arc_networkd
