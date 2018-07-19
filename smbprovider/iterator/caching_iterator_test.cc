// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>

#include "smbprovider/fake_samba_interface.h"
#include "smbprovider/iterator/caching_iterator.h"
#include "smbprovider/iterator/directory_iterator.h"
#include "smbprovider/metadata_cache.h"
#include "smbprovider/smbprovider_test_helper.h"

namespace smbprovider {

class CachingIteratorTest : public testing::Test {
 public:
  CachingIteratorTest() { cache_ = std::make_unique<MetadataCache>(); }

  ~CachingIteratorTest() override = default;

 protected:
  void CreateDefaultMountRoot() {
    fake_samba_.AddDirectory(GetDefaultServer());
    fake_samba_.AddDirectory(GetDefaultMountRoot());
  }

  CachingIterator<DirectoryIterator> GetIterator(const std::string& full_path) {
    return GetCachingIterator<DirectoryIterator>(full_path, &fake_samba_,
                                                 cache_.get());
  }

  FakeSambaInterface fake_samba_;
  std::unique_ptr<MetadataCache> cache_;

  DISALLOW_COPY_AND_ASSIGN(CachingIteratorTest);
};

TEST_F(CachingIteratorTest, NonExistantDir) {
  auto it = GetIterator("smb://non-existant-path/");
  EXPECT_EQ(ENOENT, it.Init());
}

TEST_F(CachingIteratorTest, FailsOnFile) {
  CreateDefaultMountRoot();

  fake_samba_.AddDirectory(GetAddedFullDirectoryPath());
  fake_samba_.AddFile(GetAddedFullFilePath());

  auto it = GetIterator(GetAddedFullFilePath());

  EXPECT_EQ(ENOTDIR, it.Init());
}

TEST_F(CachingIteratorTest, InitSucceedsAndSetsDoneOnEmptyDirectory) {
  CreateDefaultMountRoot();

  fake_samba_.AddDirectory(GetAddedFullDirectoryPath());

  auto it = GetIterator(GetAddedFullDirectoryPath());

  EXPECT_EQ(0, it.Init());
  EXPECT_TRUE(it.IsDone());
}

TEST_F(CachingIteratorTest, IteratorPopulatesTheCache) {
  CreateDefaultMountRoot();

  fake_samba_.AddDirectory(GetAddedFullDirectoryPath());

  const uint64_t expected_size = 99;
  const uint64_t expected_date = 88882222222;
  fake_samba_.AddFile(GetAddedFullFilePath(), expected_size, expected_date);

  auto it = GetIterator(GetAddedFullDirectoryPath());
  EXPECT_EQ(0, it.Init());
  EXPECT_FALSE(it.IsDone());

  // The cache should start empty.
  EXPECT_TRUE(cache_->IsEmpty());

  // After calling Get() the cache should be populated.
  const DirectoryEntry& entry = it.Get();
  EXPECT_FALSE(cache_->IsEmpty());

  DirectoryEntry cached_entry;
  EXPECT_TRUE(cache_->FindEntry(GetAddedFullFilePath(), &cached_entry));

  // Verify the entry returned by the iterator is correct.
  EXPECT_EQ("dog.jpg", entry.name);
  EXPECT_FALSE(entry.is_directory);
  EXPECT_EQ(expected_size, entry.size);
  EXPECT_EQ(expected_date, entry.last_modified_time);

  // Verify the entry added to the cache is correct.
  EXPECT_EQ("dog.jpg", cached_entry.name);
  EXPECT_FALSE(cached_entry.is_directory);
  EXPECT_EQ(expected_size, cached_entry.size);
  EXPECT_EQ(expected_date, cached_entry.last_modified_time);

  EXPECT_EQ(0, it.Next());
  EXPECT_TRUE(it.IsDone());
}

}  // namespace smbprovider
