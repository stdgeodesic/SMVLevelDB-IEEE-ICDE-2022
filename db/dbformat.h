// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_DBFORMAT_H_
#define STORAGE_LEVELDB_DB_DBFORMAT_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <limits>

#include "leveldb/comparator.h"
#include "leveldb/db.h"
#include "leveldb/filter_policy.h"
#include "leveldb/slice.h"
#include "leveldb/table_builder.h"

#include "util/coding.h"
#include "util/logging.h"

namespace leveldb {

// Grouping of constants.  We may want to make some of these
// parameters set via options.
namespace config {
// Default: 7. Update VersionSet::LevelSummary in version_set.cc if kNumLevels changes
static const int kNumLevels = 3;

// Level-0 compaction is started when we hit this many files.
static const int kL0_CompactionTrigger = 400; // default: 4

// Soft limit on number of level-0 files.  We slow down writes at this point.
static const int kL0_SlowdownWritesTrigger = 800; // deafult: 8

// Maximum number of level-0 files.  We stop writes at this point.
static const int kL0_StopWritesTrigger = 1200; // default: 12

// Maximum level to which a new compacted memtable is pushed if it
// does not create overlap.  We try to push to level 2 to avoid the
// relatively expensive level 0=>1 compactions and to avoid some
// expensive manifest file operations.  We do not push all the way to
// the largest level since that can generate a lot of wasted disk
// space if the same key space is being repeatedly overwritten.
static const int kMaxMemCompactLevel = 2;

// Approximate gap in bytes between samples of data read during iteration.
static const int kReadBytesPeriod = 1048576;

}  // namespace config

class InternalKey;
class MVInternalKey;

// Value types encoded as the last component of internal keys.
// DO NOT CHANGE THESE ENUM VALUES: they are embedded in the on-disk
// data structures.
enum ValueType { kTypeDeletion = 0x0, kTypeValue = 0x1 };
// kValueTypeForSeek defines the ValueType that should be passed when
// constructing a ParsedInternalKey object for seeking to a particular
// sequence number (since we sort sequence numbers in decreasing order
// and the value type is embedded as the low 8 bits in the sequence
// number in internal keys, we need to use the highest-numbered
// ValueType, not the lowest).
static const ValueType kValueTypeForSeek = kTypeValue;

typedef uint64_t SequenceNumber;

// We leave eight bits empty at the bottom so a type and sequence#
// can be packed together into 64-bits.
static const SequenceNumber kMaxSequenceNumber = ((0x1ull << 56) - 1);

// MVLevelDB: valid time type
// typedef uint64_t ValidTime; // Define in db.h
static const ValidTime kMaxValidTime = UINT64_MAX;
static const ValidTime kMinValidTime = 0x0;

struct ParsedInternalKey {
  Slice user_key;
  SequenceNumber sequence;
  ValueType type;

  ParsedInternalKey() {}  // Intentionally left uninitialized (for speed)
  ParsedInternalKey(const Slice& u, const SequenceNumber& seq, ValueType t)
      : user_key(u), sequence(seq), type(t) {}
  std::string DebugString() const;
};

struct ParsedMVInternalKey {
  Slice user_key;
  SequenceNumber sequence;
  ValueType type;
  ValidTime valid_time;  // Start valid time

  ParsedMVInternalKey() {}
  ParsedMVInternalKey(const Slice& u, const SequenceNumber& seq, ValueType t,
                        ValidTime vt)
  : user_key(u), sequence(seq), type(t), valid_time(vt) {}
};

// Return the length of the encoding of "key".
inline size_t InternalKeyEncodingLength(const ParsedInternalKey& key) {
  return key.user_key.size() + 8;
}

inline size_t MVInternalKeyEncodingLength(const ParsedMVInternalKey& key) {
  return key.user_key.size() + 16;
}

// Append the serialization of "key" to *result.
void AppendInternalKey(std::string* result, const ParsedInternalKey& key);

void AppendMVInternalKey(std::string* result, const ParsedMVInternalKey& key);

// Attempt to parse an internal key from "internal_key".  On success,
// stores the parsed data in "*result", and returns true.
//
// On error, returns false, leaves "*result" in an undefined state.
bool ParseInternalKey(const Slice& internal_key, ParsedInternalKey* result);

bool ParseMVInternalKey(const Slice& mv_internal_key, ParsedMVInternalKey* result);

// Returns the user key portion of an internal key.
inline Slice ExtractUserKey(const Slice& internal_key) {
  assert(internal_key.size() >= 8);
  return Slice(internal_key.data(), internal_key.size() - 8);
}

inline Slice MVExtractUserKey(const Slice& mv_internal_key) {
  assert(mv_internal_key.size() >= 16);
  return Slice(mv_internal_key.data(), mv_internal_key.size() - 16);
}

// A comparator for internal keys that uses a specified comparator for
// the user key portion and breaks ties by decreasing sequence number.
class InternalKeyComparator : public Comparator {
 private:
  const Comparator* user_comparator_;
  // MVLevelDB: set multi_version to TRUE to omit timestamp field when extract user key
  const bool multi_version = false;

 public:
  explicit InternalKeyComparator(const Comparator* c) : user_comparator_(c) {}
  explicit InternalKeyComparator(const Comparator* c, bool mv) : user_comparator_(c), multi_version(mv) {}
  const char* Name() const override;
  int Compare(const Slice& a, const Slice& b) const override;
  void FindShortestSeparator(std::string* start,
                             const Slice& limit) const override;
  void FindShortSuccessor(std::string* key) const override;

  const Comparator* user_comparator() const { return user_comparator_; }

  int Compare(const InternalKey& a, const InternalKey& b) const;
};
//// The internal key comparator used in MVLevelDB
//class MVInternalKeyComparator : public InternalKeyComparator {
// private:
//  const Comparator* user_comparator_;
//
// public:
//  explicit MVInternalKeyComparator(const Comparator* c) : InternalKeyComparator(c), user_comparator_(c) {}
//  const char* Name() const override;
//  int Compare(const Slice& a, const Slice& b) const override;
//  void FindShortestSeparator(std::string* start,
//                             const Slice& limit) const override;
//  void FindShortSuccessor(std::string* key) const override;
//
//  const Comparator* user_comparator() const { return user_comparator_; }
//
//  int Compare(const MVInternalKey& a, const MVInternalKey& b) const;
//};

// Filter policy wrapper that converts from internal keys to user keys
class InternalFilterPolicy : public FilterPolicy {
 private:
  const FilterPolicy* const user_policy_;

 public:
  explicit InternalFilterPolicy(const FilterPolicy* p) : user_policy_(p) {}
  const char* Name() const override;
  void CreateFilter(const Slice* keys, int n, std::string* dst) const override;
  bool KeyMayMatch(const Slice& key, const Slice& filter) const override;
};

// Modules in this directory should keep internal keys wrapped inside
// the following class instead of plain strings so that we do not
// incorrectly use string comparisons instead of an InternalKeyComparator.
class InternalKey {
 private:
  std::string rep_;

 public:
  InternalKey() {}  // Leave rep_ as empty to indicate it is invalid
  InternalKey(const Slice& user_key, SequenceNumber s, ValueType t) {
    AppendInternalKey(&rep_, ParsedInternalKey(user_key, s, t));
  }

  bool DecodeFrom(const Slice& s) {
    rep_.assign(s.data(), s.size());
    return !rep_.empty();
  }

  // Decode from MVInternalKey string
  bool DecodeFromMV(const Slice& s) {
    rep_.assign(s.data(), s.size() - 8);
    return !rep_.empty();
  }

  Slice Encode() const {
    assert(!rep_.empty());
    return rep_;
  }

  Slice user_key() const { return ExtractUserKey(rep_); }

  void SetFrom(const ParsedInternalKey& p) {
    rep_.clear();
    AppendInternalKey(&rep_, p);
  }

  void Clear() { rep_.clear(); }

  std::string DebugString() const;
};

class MVInternalKey {
 private:
  std::string rep_;

 public:
  MVInternalKey() {}  // Leave rep_ as empty to indicate it is valid
  MVInternalKey(const Slice& user_key, SequenceNumber s, ValueType t,
                ValidTime vt) {
    AppendMVInternalKey(&rep_, ParsedMVInternalKey(user_key, s, t, vt));
  }

  bool DecodeFrom(const Slice& s) {
    rep_.assign(s.data(), s.size());
    return !rep_.empty();
  }

  Slice Encode() const {
    assert(!rep_.empty());
    return rep_;
  }

  Slice user_key() const { return MVExtractUserKey(rep_); }

  void SetFrom(const ParsedMVInternalKey& p) {
    rep_.clear();
    AppendMVInternalKey(&rep_, p);
  }

  void Clear() { rep_.clear(); }
};

inline int InternalKeyComparator::Compare(const InternalKey& a,
                                          const InternalKey& b) const {
  return Compare(a.Encode(), b.Encode());
}

//inline int MVInternalKeyComparator::Compare(const MVInternalKey& a,
//                                          const MVInternalKey& b) const {
//  return Compare(a.Encode(), b.Encode());
//}

inline bool ParseInternalKey(const Slice& internal_key,
                             ParsedInternalKey* result) {
  const size_t n = internal_key.size();
  if (n < 8) return false;
  uint64_t num = DecodeFixed64(internal_key.data() + n - 8);
  uint8_t c = num & 0xff;
  result->sequence = num >> 8;
  result->type = static_cast<ValueType>(c);
  result->user_key = Slice(internal_key.data(), n - 8);
  return (c <= static_cast<uint8_t>(kTypeValue));
}

inline bool ParseMVInternalKey(const Slice& mv_internal_key,
                        ParsedMVInternalKey* result) {
  const size_t n = mv_internal_key.size();
  if (n < 16) return false;
  uint64_t num = DecodeFixed64(mv_internal_key.data() + n - 16);
  uint8_t c = num & 0xff;
  result->sequence = num >> 8;
  result->type = static_cast<ValueType>(c);
  result->user_key = Slice(mv_internal_key.data(), n - 16);
  result->valid_time = DecodeFixed64(mv_internal_key.data() + n - 8);
  return (c <= static_cast<uint8_t>(kTypeValue));
}

// A helper class useful for DBImpl::Get()
class LookupKey {
 public:
  // Initialize *this for looking up user_key at a snapshot with
  // the specified sequence number.
  LookupKey(const Slice& user_key, SequenceNumber sequence);

  LookupKey(const LookupKey&) = delete;
  LookupKey& operator=(const LookupKey&) = delete;

  ~LookupKey();

  // Return a key suitable for lookup in a MemTable.
  Slice memtable_key() const { return Slice(start_, end_ - start_); }

  // Return an internal key (suitable for passing to an internal iterator)
  Slice internal_key() const { return Slice(kstart_, end_ - kstart_); }

  // Return the user key
  Slice user_key() const { return Slice(kstart_, end_ - kstart_ - 8); }

 private:
  // We construct a char array of the form:
  //    klength  varint32               <-- start_
  //    userkey  char[klength]          <-- kstart_
  //    tag      uint64
  //                                    <-- end_
  // The array is a suitable MemTable key.
  // The suffix starting with "userkey" can be used as an InternalKey.
  const char* start_;
  const char* kstart_;
  const char* end_;
  char space_[200];  // Avoid allocation for short keys
};

inline LookupKey::~LookupKey() {
  if (start_ != space_) delete[] start_;
}

class MVLookupKey {
 public:
  MVLookupKey(const Slice& user_key, SequenceNumber sequence,
              ValidTime t);

  MVLookupKey(const MVLookupKey&) = delete;
  MVLookupKey& operator=(const MVLookupKey&) = delete;

  ~MVLookupKey();

  // Return a key suitable for lookup in a MemTable.
  Slice memtable_key() const { return Slice(start_, end_ - start_); }

  // Return an internal key (suitable for passing to an internal iterator)
  Slice internal_key() const { return Slice(kstart_, end_ - kstart_); }

  // Return the user key
  // 16 = sizeof (seq + tag + ValidTime)
  Slice user_key() const { return Slice(kstart_, end_ - kstart_ - 16); }

  // Return the valid time field
  ValidTime valid_time() const { return DecodeFixed64(end_ - 8); }

 private:
  const char* start_;
  const char* kstart_;
  const char* end_;
  char space_[200]; // Avoid allocation for short keys
};

inline MVLookupKey::~MVLookupKey() {
  if (start_ != space_) delete[] start_;
}

// Convenience methods for extracting valid time from internal keys


}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_DBFORMAT_H_
