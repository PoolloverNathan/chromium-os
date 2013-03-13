// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_mapper.h"

#include <base/logging.h>

bool AddressMapper::Map(const uint64 real_addr,
                        const uint64 size,
                        const bool remove_existing_mappings) {
  return MapWithName(real_addr, size, "", remove_existing_mappings);
}

bool AddressMapper::MapWithName(const uint64 real_addr,
                                const uint64 size,
                                const string& name,
                                bool remove_existing_mappings) {
  MappedRange range;
  range.real_addr = real_addr;
  range.size = size;
  if (name.size())
    range.name = name;

  if (size == 0) {
    LOG(ERROR) << "Must allocate a nonzero-length address range.";
    return false;
  }

  // Check that this mapping does not overflow the address space.
  if (real_addr + size - 1 != kuint64max &&
      !(real_addr + size > real_addr)) {
    LOG(ERROR) << "Address mapping at " << (void*)real_addr << " with size "
               << (void*)size << " overflows.";
    return false;
  }

  // Check for collision with an existing mapping.  This must be an overlap that
  // does not result in one range being completely covered by another
  MappingList::iterator iter;
  MappingList mappings_to_delete;
  MappedRange* covering_range = NULL;
  MappingList::iterator covering_range_iter;
  for (iter = mappings_.begin(); iter != mappings_.end(); ++iter) {
    if (!iter->Intersects(range))
      continue;
    if (!covering_range && iter->Covers(range) && iter->size > range.size) {
      covering_range = &(*iter);
      covering_range_iter = iter;
      continue;
    }
    // Quit if existing ranges that collide aren't supposed to be removed.
    if (!remove_existing_mappings)
      return false;
    mappings_to_delete.push_back(*iter);
  }

  while (!mappings_to_delete.empty()) {
    const MappedRange& range = mappings_to_delete.front();
    CHECK(Unmap(range));
    mappings_to_delete.pop_front();
  }

  // Otherwise check for this range being covered by another range.  If that
  // happens, split or reduce the existing range to make room.
  if (covering_range) {
    MappedRange old_range = *covering_range;
    CHECK(Unmap(old_range));

    uint64 gap_before = range.real_addr - old_range.real_addr;
    uint64 gap_after = (old_range.real_addr + old_range.size) -
                       (range.real_addr + range.size);
    if (gap_before)
      CHECK(Map(old_range.real_addr, gap_before, false));
    CHECK(Map(range.real_addr, range.size, false));
    if (gap_after)
      CHECK(Map(range.real_addr + range.size, gap_after, false));
  }

  // Now search for a location for the new range.  It should be in the first
  // free block in quipper space.

  // If there is no existing mapping, add it to the beginning of quipper space.
  if (mappings_.empty()) {
    range.mapped_addr = 0;
    range.unmapped_space_after = kuint64max - range.size;
    mappings_.push_back(range);
    return true;
  }

  // If there is space before the first mapped range in quipper space, use it.
  if (mappings_.begin()->mapped_addr >= range.size) {
    range.mapped_addr = 0;
    range.unmapped_space_after = mappings_.begin()->mapped_addr - range.size;
    mappings_.push_front(range);
    return true;
  }

  // Otherwise, search through the existing mappings for a free block after one
  // of them.
  for (iter = mappings_.begin(); iter != mappings_.end(); ++iter) {
    if (iter->unmapped_space_after < range.size)
      continue;

    range.mapped_addr = iter->mapped_addr + iter->size;
    range.unmapped_space_after = iter->unmapped_space_after - range.size;
    iter->unmapped_space_after = 0;

    mappings_.insert(++iter, range);
    return true;
  }

  // If it still hasn't succeeded in mapping, it means there is no free space in
  // quipper space large enough for a mapping of this size.
  LOG(ERROR) << "Could not find space to map addr=" << (void*) real_addr
             << " with size " << (void*) size;
  return false;
}

bool AddressMapper::GetMappedAddress(const uint64 real_addr,
                                     uint64* mapped_addr) const {
  CHECK(mapped_addr);
  MappingList::const_iterator iter;
  for (iter = mappings_.begin(); iter != mappings_.end(); ++iter) {
    if (!iter->ContainsAddress(real_addr))
      continue;
    *mapped_addr = iter->mapped_addr + real_addr - iter->real_addr;
    return true;
  }
  return false;
}

bool AddressMapper::GetMappedNameAndOffset(const uint64 real_addr,
                                           string* name,
                                           uint64* offset) const {
  CHECK(name);
  CHECK(offset);
  MappingList::const_iterator iter;
  for (iter = mappings_.begin(); iter != mappings_.end(); ++iter) {
    if (!iter->ContainsAddress(real_addr))
      continue;
    *name = iter->name;
    *offset = real_addr - iter->real_addr;
    return true;
  }
  return false;
}

bool AddressMapper::Unmap(const MappedRange& range) {
  MappingList::iterator iter;
  // TODO(sque): this is highly inefficient since Unmap() is called from a
  // function that has already iterated to the right place within |mappings_|.
  // For a first revision, I am sacrificing efficiency for of clarity, due to
  // the trickiness of removing elements using iterators.
  for (iter = mappings_.begin(); iter != mappings_.end(); ++iter) {
    if (range.real_addr == iter->real_addr && range.size == iter->size) {
      // Add the freed up space to the free space counter of the previous
      // mapped region, if it exists.
      if (iter != mappings_.begin()) {
        --iter;
        iter->unmapped_space_after += range.size + range.unmapped_space_after;
        ++iter;
      }
      mappings_.erase(iter);
      return true;
    }
  }
  return false;
}
