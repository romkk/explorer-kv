// automatically generated by the FlatBuffers compiler, do not modify

#ifndef FLATBUFFERS_GENERATED_EXPLORER_FBE_H_
#define FLATBUFFERS_GENERATED_EXPLORER_FBE_H_

#include "flatbuffers/flatbuffers.h"


namespace fbe {

struct TxHex;
struct TxInput;
struct TxOutput;
struct Tx;
struct TxSpentBy;
struct BlockHash;
struct Block;
struct BlockTxsHash;
struct Address;
struct AddressTx;
struct AddressTxIdx;
struct UnspentOutput;
struct UnspentOutputIdx;
struct DoubleSpending;

MANUALLY_ALIGNED_STRUCT(8) Address FLATBUFFERS_FINAL_CLASS {
 private:
  int32_t tx_count_;
  int32_t __padding0;
  int64_t received_;
  int64_t sent_;
  int32_t unconfirmed_tx_count_;
  int32_t __padding1;
  int64_t unconfirmed_received_;
  int64_t unconfirmed_sent_;
  int32_t last_confirmed_tx_idx_;
  uint32_t created_at_;
  uint32_t updated_at_;
  int32_t __padding2;

 public:
  Address(int32_t tx_count, int64_t received, int64_t sent, int32_t unconfirmed_tx_count, int64_t unconfirmed_received, int64_t unconfirmed_sent, int32_t last_confirmed_tx_idx, uint32_t created_at, uint32_t updated_at)
    : tx_count_(flatbuffers::EndianScalar(tx_count)), __padding0(0), received_(flatbuffers::EndianScalar(received)), sent_(flatbuffers::EndianScalar(sent)), unconfirmed_tx_count_(flatbuffers::EndianScalar(unconfirmed_tx_count)), __padding1(0), unconfirmed_received_(flatbuffers::EndianScalar(unconfirmed_received)), unconfirmed_sent_(flatbuffers::EndianScalar(unconfirmed_sent)), last_confirmed_tx_idx_(flatbuffers::EndianScalar(last_confirmed_tx_idx)), created_at_(flatbuffers::EndianScalar(created_at)), updated_at_(flatbuffers::EndianScalar(updated_at)), __padding2(0) { (void)__padding0; (void)__padding1; (void)__padding2; }

  int32_t tx_count() const { return flatbuffers::EndianScalar(tx_count_); }
  void mutate_tx_count(int32_t tx_count) { flatbuffers::WriteScalar(&tx_count_, tx_count); }
  int64_t received() const { return flatbuffers::EndianScalar(received_); }
  void mutate_received(int64_t received) { flatbuffers::WriteScalar(&received_, received); }
  int64_t sent() const { return flatbuffers::EndianScalar(sent_); }
  void mutate_sent(int64_t sent) { flatbuffers::WriteScalar(&sent_, sent); }
  int32_t unconfirmed_tx_count() const { return flatbuffers::EndianScalar(unconfirmed_tx_count_); }
  void mutate_unconfirmed_tx_count(int32_t unconfirmed_tx_count) { flatbuffers::WriteScalar(&unconfirmed_tx_count_, unconfirmed_tx_count); }
  int64_t unconfirmed_received() const { return flatbuffers::EndianScalar(unconfirmed_received_); }
  void mutate_unconfirmed_received(int64_t unconfirmed_received) { flatbuffers::WriteScalar(&unconfirmed_received_, unconfirmed_received); }
  int64_t unconfirmed_sent() const { return flatbuffers::EndianScalar(unconfirmed_sent_); }
  void mutate_unconfirmed_sent(int64_t unconfirmed_sent) { flatbuffers::WriteScalar(&unconfirmed_sent_, unconfirmed_sent); }
  int32_t last_confirmed_tx_idx() const { return flatbuffers::EndianScalar(last_confirmed_tx_idx_); }
  void mutate_last_confirmed_tx_idx(int32_t last_confirmed_tx_idx) { flatbuffers::WriteScalar(&last_confirmed_tx_idx_, last_confirmed_tx_idx); }
  uint32_t created_at() const { return flatbuffers::EndianScalar(created_at_); }
  void mutate_created_at(uint32_t created_at) { flatbuffers::WriteScalar(&created_at_, created_at); }
  uint32_t updated_at() const { return flatbuffers::EndianScalar(updated_at_); }
  void mutate_updated_at(uint32_t updated_at) { flatbuffers::WriteScalar(&updated_at_, updated_at); }
};
STRUCT_END(Address, 64);

struct TxHex FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  const flatbuffers::String *hex() const { return GetPointer<const flatbuffers::String *>(4); }
  flatbuffers::String *mutable_hex() { return GetPointer<flatbuffers::String *>(4); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 4 /* hex */) &&
           verifier.Verify(hex()) &&
           verifier.EndTable();
  }
};

struct TxHexBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_hex(flatbuffers::Offset<flatbuffers::String> hex) { fbb_.AddOffset(4, hex); }
  TxHexBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  TxHexBuilder &operator=(const TxHexBuilder &);
  flatbuffers::Offset<TxHex> Finish() {
    auto o = flatbuffers::Offset<TxHex>(fbb_.EndTable(start_, 1));
    return o;
  }
};

inline flatbuffers::Offset<TxHex> CreateTxHex(flatbuffers::FlatBufferBuilder &_fbb,
   flatbuffers::Offset<flatbuffers::String> hex = 0) {
  TxHexBuilder builder_(_fbb);
  builder_.add_hex(hex);
  return builder_.Finish();
}

struct TxInput FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  const flatbuffers::String *script_asm() const { return GetPointer<const flatbuffers::String *>(4); }
  flatbuffers::String *mutable_script_asm() { return GetPointer<flatbuffers::String *>(4); }
  const flatbuffers::String *script_hex() const { return GetPointer<const flatbuffers::String *>(6); }
  flatbuffers::String *mutable_script_hex() { return GetPointer<flatbuffers::String *>(6); }
  int64_t sequence() const { return GetField<int64_t>(8, 0); }
  bool mutate_sequence(int64_t sequence) { return SetField(8, sequence); }
  const flatbuffers::String *prev_tx_hash() const { return GetPointer<const flatbuffers::String *>(10); }
  flatbuffers::String *mutable_prev_tx_hash() { return GetPointer<flatbuffers::String *>(10); }
  int32_t prev_position() const { return GetField<int32_t>(12, -1); }
  bool mutate_prev_position(int32_t prev_position) { return SetField(12, prev_position); }
  int64_t prev_value() const { return GetField<int64_t>(14, 0); }
  bool mutate_prev_value(int64_t prev_value) { return SetField(14, prev_value); }
  const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *prev_addresses() const { return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *>(16); }
  flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *mutable_prev_addresses() { return GetPointer<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *>(16); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 4 /* script_asm */) &&
           verifier.Verify(script_asm()) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 6 /* script_hex */) &&
           verifier.Verify(script_hex()) &&
           VerifyField<int64_t>(verifier, 8 /* sequence */) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 10 /* prev_tx_hash */) &&
           verifier.Verify(prev_tx_hash()) &&
           VerifyField<int32_t>(verifier, 12 /* prev_position */) &&
           VerifyField<int64_t>(verifier, 14 /* prev_value */) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 16 /* prev_addresses */) &&
           verifier.Verify(prev_addresses()) &&
           verifier.VerifyVectorOfStrings(prev_addresses()) &&
           verifier.EndTable();
  }
};

struct TxInputBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_script_asm(flatbuffers::Offset<flatbuffers::String> script_asm) { fbb_.AddOffset(4, script_asm); }
  void add_script_hex(flatbuffers::Offset<flatbuffers::String> script_hex) { fbb_.AddOffset(6, script_hex); }
  void add_sequence(int64_t sequence) { fbb_.AddElement<int64_t>(8, sequence, 0); }
  void add_prev_tx_hash(flatbuffers::Offset<flatbuffers::String> prev_tx_hash) { fbb_.AddOffset(10, prev_tx_hash); }
  void add_prev_position(int32_t prev_position) { fbb_.AddElement<int32_t>(12, prev_position, -1); }
  void add_prev_value(int64_t prev_value) { fbb_.AddElement<int64_t>(14, prev_value, 0); }
  void add_prev_addresses(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> prev_addresses) { fbb_.AddOffset(16, prev_addresses); }
  TxInputBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  TxInputBuilder &operator=(const TxInputBuilder &);
  flatbuffers::Offset<TxInput> Finish() {
    auto o = flatbuffers::Offset<TxInput>(fbb_.EndTable(start_, 7));
    return o;
  }
};

inline flatbuffers::Offset<TxInput> CreateTxInput(flatbuffers::FlatBufferBuilder &_fbb,
   flatbuffers::Offset<flatbuffers::String> script_asm = 0,
   flatbuffers::Offset<flatbuffers::String> script_hex = 0,
   int64_t sequence = 0,
   flatbuffers::Offset<flatbuffers::String> prev_tx_hash = 0,
   int32_t prev_position = -1,
   int64_t prev_value = 0,
   flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> prev_addresses = 0) {
  TxInputBuilder builder_(_fbb);
  builder_.add_prev_value(prev_value);
  builder_.add_sequence(sequence);
  builder_.add_prev_addresses(prev_addresses);
  builder_.add_prev_position(prev_position);
  builder_.add_prev_tx_hash(prev_tx_hash);
  builder_.add_script_hex(script_hex);
  builder_.add_script_asm(script_asm);
  return builder_.Finish();
}

struct TxOutput FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *addresses() const { return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *>(4); }
  flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *mutable_addresses() { return GetPointer<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *>(4); }
  int64_t value() const { return GetField<int64_t>(6, 0); }
  bool mutate_value(int64_t value) { return SetField(6, value); }
  const flatbuffers::String *script_asm() const { return GetPointer<const flatbuffers::String *>(8); }
  flatbuffers::String *mutable_script_asm() { return GetPointer<flatbuffers::String *>(8); }
  const flatbuffers::String *script_hex() const { return GetPointer<const flatbuffers::String *>(10); }
  flatbuffers::String *mutable_script_hex() { return GetPointer<flatbuffers::String *>(10); }
  const flatbuffers::String *script_type() const { return GetPointer<const flatbuffers::String *>(12); }
  flatbuffers::String *mutable_script_type() { return GetPointer<flatbuffers::String *>(12); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 4 /* addresses */) &&
           verifier.Verify(addresses()) &&
           verifier.VerifyVectorOfStrings(addresses()) &&
           VerifyField<int64_t>(verifier, 6 /* value */) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 8 /* script_asm */) &&
           verifier.Verify(script_asm()) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 10 /* script_hex */) &&
           verifier.Verify(script_hex()) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 12 /* script_type */) &&
           verifier.Verify(script_type()) &&
           verifier.EndTable();
  }
};

struct TxOutputBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_addresses(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> addresses) { fbb_.AddOffset(4, addresses); }
  void add_value(int64_t value) { fbb_.AddElement<int64_t>(6, value, 0); }
  void add_script_asm(flatbuffers::Offset<flatbuffers::String> script_asm) { fbb_.AddOffset(8, script_asm); }
  void add_script_hex(flatbuffers::Offset<flatbuffers::String> script_hex) { fbb_.AddOffset(10, script_hex); }
  void add_script_type(flatbuffers::Offset<flatbuffers::String> script_type) { fbb_.AddOffset(12, script_type); }
  TxOutputBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  TxOutputBuilder &operator=(const TxOutputBuilder &);
  flatbuffers::Offset<TxOutput> Finish() {
    auto o = flatbuffers::Offset<TxOutput>(fbb_.EndTable(start_, 5));
    return o;
  }
};

inline flatbuffers::Offset<TxOutput> CreateTxOutput(flatbuffers::FlatBufferBuilder &_fbb,
   flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> addresses = 0,
   int64_t value = 0,
   flatbuffers::Offset<flatbuffers::String> script_asm = 0,
   flatbuffers::Offset<flatbuffers::String> script_hex = 0,
   flatbuffers::Offset<flatbuffers::String> script_type = 0) {
  TxOutputBuilder builder_(_fbb);
  builder_.add_value(value);
  builder_.add_script_type(script_type);
  builder_.add_script_hex(script_hex);
  builder_.add_script_asm(script_asm);
  builder_.add_addresses(addresses);
  return builder_.Finish();
}

struct Tx FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  int32_t height() const { return GetField<int32_t>(4, -1); }
  bool mutate_height(int32_t height) { return SetField(4, height); }
  int32_t position_in_block() const { return GetField<int32_t>(6, -1); }
  bool mutate_position_in_block(int32_t position_in_block) { return SetField(6, position_in_block); }
  uint8_t is_coinbase() const { return GetField<uint8_t>(8, 0); }
  bool mutate_is_coinbase(uint8_t is_coinbase) { return SetField(8, is_coinbase); }
  int32_t version() const { return GetField<int32_t>(10, 0); }
  bool mutate_version(int32_t version) { return SetField(10, version); }
  uint32_t lock_time() const { return GetField<uint32_t>(12, 0); }
  bool mutate_lock_time(uint32_t lock_time) { return SetField(12, lock_time); }
  int32_t size() const { return GetField<int32_t>(14, 0); }
  bool mutate_size(int32_t size) { return SetField(14, size); }
  int64_t fee() const { return GetField<int64_t>(16, 0); }
  bool mutate_fee(int64_t fee) { return SetField(16, fee); }
  const flatbuffers::Vector<flatbuffers::Offset<TxInput>> *inputs() const { return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<TxInput>> *>(18); }
  flatbuffers::Vector<flatbuffers::Offset<TxInput>> *mutable_inputs() { return GetPointer<flatbuffers::Vector<flatbuffers::Offset<TxInput>> *>(18); }
  int32_t inputs_count() const { return GetField<int32_t>(20, 0); }
  bool mutate_inputs_count(int32_t inputs_count) { return SetField(20, inputs_count); }
  int64_t inputs_value() const { return GetField<int64_t>(22, 0); }
  bool mutate_inputs_value(int64_t inputs_value) { return SetField(22, inputs_value); }
  const flatbuffers::Vector<flatbuffers::Offset<TxOutput>> *outputs() const { return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<TxOutput>> *>(24); }
  flatbuffers::Vector<flatbuffers::Offset<TxOutput>> *mutable_outputs() { return GetPointer<flatbuffers::Vector<flatbuffers::Offset<TxOutput>> *>(24); }
  int32_t outputs_count() const { return GetField<int32_t>(26, 0); }
  bool mutate_outputs_count(int32_t outputs_count) { return SetField(26, outputs_count); }
  int64_t outputs_value() const { return GetField<int64_t>(28, 0); }
  bool mutate_outputs_value(int64_t outputs_value) { return SetField(28, outputs_value); }
  const flatbuffers::String *created_at() const { return GetPointer<const flatbuffers::String *>(30); }
  flatbuffers::String *mutable_created_at() { return GetPointer<flatbuffers::String *>(30); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<int32_t>(verifier, 4 /* height */) &&
           VerifyField<int32_t>(verifier, 6 /* position_in_block */) &&
           VerifyField<uint8_t>(verifier, 8 /* is_coinbase */) &&
           VerifyField<int32_t>(verifier, 10 /* version */) &&
           VerifyField<uint32_t>(verifier, 12 /* lock_time */) &&
           VerifyField<int32_t>(verifier, 14 /* size */) &&
           VerifyField<int64_t>(verifier, 16 /* fee */) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 18 /* inputs */) &&
           verifier.Verify(inputs()) &&
           verifier.VerifyVectorOfTables(inputs()) &&
           VerifyField<int32_t>(verifier, 20 /* inputs_count */) &&
           VerifyField<int64_t>(verifier, 22 /* inputs_value */) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 24 /* outputs */) &&
           verifier.Verify(outputs()) &&
           verifier.VerifyVectorOfTables(outputs()) &&
           VerifyField<int32_t>(verifier, 26 /* outputs_count */) &&
           VerifyField<int64_t>(verifier, 28 /* outputs_value */) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 30 /* created_at */) &&
           verifier.Verify(created_at()) &&
           verifier.EndTable();
  }
};

struct TxBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_height(int32_t height) { fbb_.AddElement<int32_t>(4, height, -1); }
  void add_position_in_block(int32_t position_in_block) { fbb_.AddElement<int32_t>(6, position_in_block, -1); }
  void add_is_coinbase(uint8_t is_coinbase) { fbb_.AddElement<uint8_t>(8, is_coinbase, 0); }
  void add_version(int32_t version) { fbb_.AddElement<int32_t>(10, version, 0); }
  void add_lock_time(uint32_t lock_time) { fbb_.AddElement<uint32_t>(12, lock_time, 0); }
  void add_size(int32_t size) { fbb_.AddElement<int32_t>(14, size, 0); }
  void add_fee(int64_t fee) { fbb_.AddElement<int64_t>(16, fee, 0); }
  void add_inputs(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<TxInput>>> inputs) { fbb_.AddOffset(18, inputs); }
  void add_inputs_count(int32_t inputs_count) { fbb_.AddElement<int32_t>(20, inputs_count, 0); }
  void add_inputs_value(int64_t inputs_value) { fbb_.AddElement<int64_t>(22, inputs_value, 0); }
  void add_outputs(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<TxOutput>>> outputs) { fbb_.AddOffset(24, outputs); }
  void add_outputs_count(int32_t outputs_count) { fbb_.AddElement<int32_t>(26, outputs_count, 0); }
  void add_outputs_value(int64_t outputs_value) { fbb_.AddElement<int64_t>(28, outputs_value, 0); }
  void add_created_at(flatbuffers::Offset<flatbuffers::String> created_at) { fbb_.AddOffset(30, created_at); }
  TxBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  TxBuilder &operator=(const TxBuilder &);
  flatbuffers::Offset<Tx> Finish() {
    auto o = flatbuffers::Offset<Tx>(fbb_.EndTable(start_, 14));
    return o;
  }
};

inline flatbuffers::Offset<Tx> CreateTx(flatbuffers::FlatBufferBuilder &_fbb,
   int32_t height = -1,
   int32_t position_in_block = -1,
   uint8_t is_coinbase = 0,
   int32_t version = 0,
   uint32_t lock_time = 0,
   int32_t size = 0,
   int64_t fee = 0,
   flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<TxInput>>> inputs = 0,
   int32_t inputs_count = 0,
   int64_t inputs_value = 0,
   flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<TxOutput>>> outputs = 0,
   int32_t outputs_count = 0,
   int64_t outputs_value = 0,
   flatbuffers::Offset<flatbuffers::String> created_at = 0) {
  TxBuilder builder_(_fbb);
  builder_.add_outputs_value(outputs_value);
  builder_.add_inputs_value(inputs_value);
  builder_.add_fee(fee);
  builder_.add_created_at(created_at);
  builder_.add_outputs_count(outputs_count);
  builder_.add_outputs(outputs);
  builder_.add_inputs_count(inputs_count);
  builder_.add_inputs(inputs);
  builder_.add_size(size);
  builder_.add_lock_time(lock_time);
  builder_.add_version(version);
  builder_.add_position_in_block(position_in_block);
  builder_.add_height(height);
  builder_.add_is_coinbase(is_coinbase);
  return builder_.Finish();
}

struct TxSpentBy FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  const flatbuffers::String *tx_hash() const { return GetPointer<const flatbuffers::String *>(4); }
  flatbuffers::String *mutable_tx_hash() { return GetPointer<flatbuffers::String *>(4); }
  int32_t position() const { return GetField<int32_t>(6, 0); }
  bool mutate_position(int32_t position) { return SetField(6, position); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 4 /* tx_hash */) &&
           verifier.Verify(tx_hash()) &&
           VerifyField<int32_t>(verifier, 6 /* position */) &&
           verifier.EndTable();
  }
};

struct TxSpentByBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_tx_hash(flatbuffers::Offset<flatbuffers::String> tx_hash) { fbb_.AddOffset(4, tx_hash); }
  void add_position(int32_t position) { fbb_.AddElement<int32_t>(6, position, 0); }
  TxSpentByBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  TxSpentByBuilder &operator=(const TxSpentByBuilder &);
  flatbuffers::Offset<TxSpentBy> Finish() {
    auto o = flatbuffers::Offset<TxSpentBy>(fbb_.EndTable(start_, 2));
    return o;
  }
};

inline flatbuffers::Offset<TxSpentBy> CreateTxSpentBy(flatbuffers::FlatBufferBuilder &_fbb,
   flatbuffers::Offset<flatbuffers::String> tx_hash = 0,
   int32_t position = 0) {
  TxSpentByBuilder builder_(_fbb);
  builder_.add_position(position);
  builder_.add_tx_hash(tx_hash);
  return builder_.Finish();
}

struct BlockHash FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  const flatbuffers::String *hash() const { return GetPointer<const flatbuffers::String *>(4); }
  flatbuffers::String *mutable_hash() { return GetPointer<flatbuffers::String *>(4); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 4 /* hash */) &&
           verifier.Verify(hash()) &&
           verifier.EndTable();
  }
};

struct BlockHashBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_hash(flatbuffers::Offset<flatbuffers::String> hash) { fbb_.AddOffset(4, hash); }
  BlockHashBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  BlockHashBuilder &operator=(const BlockHashBuilder &);
  flatbuffers::Offset<BlockHash> Finish() {
    auto o = flatbuffers::Offset<BlockHash>(fbb_.EndTable(start_, 1));
    return o;
  }
};

inline flatbuffers::Offset<BlockHash> CreateBlockHash(flatbuffers::FlatBufferBuilder &_fbb,
   flatbuffers::Offset<flatbuffers::String> hash = 0) {
  BlockHashBuilder builder_(_fbb);
  builder_.add_hash(hash);
  return builder_.Finish();
}

struct Block FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  int32_t version() const { return GetField<int32_t>(4, 0); }
  bool mutate_version(int32_t version) { return SetField(4, version); }
  const flatbuffers::String *mrkl_root() const { return GetPointer<const flatbuffers::String *>(6); }
  flatbuffers::String *mutable_mrkl_root() { return GetPointer<flatbuffers::String *>(6); }
  uint32_t timestamp() const { return GetField<uint32_t>(8, 0); }
  bool mutate_timestamp(uint32_t timestamp) { return SetField(8, timestamp); }
  uint32_t curr_max_timestamp() const { return GetField<uint32_t>(10, 0); }
  bool mutate_curr_max_timestamp(uint32_t curr_max_timestamp) { return SetField(10, curr_max_timestamp); }
  uint32_t bits() const { return GetField<uint32_t>(12, 0); }
  bool mutate_bits(uint32_t bits) { return SetField(12, bits); }
  uint32_t nonce() const { return GetField<uint32_t>(14, 0); }
  bool mutate_nonce(uint32_t nonce) { return SetField(14, nonce); }
  const flatbuffers::String *prev_block_hash() const { return GetPointer<const flatbuffers::String *>(16); }
  flatbuffers::String *mutable_prev_block_hash() { return GetPointer<flatbuffers::String *>(16); }
  const flatbuffers::String *next_block_hash() const { return GetPointer<const flatbuffers::String *>(18); }
  flatbuffers::String *mutable_next_block_hash() { return GetPointer<flatbuffers::String *>(18); }
  int32_t size() const { return GetField<int32_t>(20, 0); }
  bool mutate_size(int32_t size) { return SetField(20, size); }
  int64_t pool_difficulty() const { return GetField<int64_t>(22, 0); }
  bool mutate_pool_difficulty(int64_t pool_difficulty) { return SetField(22, pool_difficulty); }
  double difficulty() const { return GetField<double>(24, 0); }
  bool mutate_difficulty(double difficulty) { return SetField(24, difficulty); }
  uint32_t tx_count() const { return GetField<uint32_t>(26, 0); }
  bool mutate_tx_count(uint32_t tx_count) { return SetField(26, tx_count); }
  int64_t reward_block() const { return GetField<int64_t>(28, 0); }
  bool mutate_reward_block(int64_t reward_block) { return SetField(28, reward_block); }
  int64_t reward_fees() const { return GetField<int64_t>(30, 0); }
  bool mutate_reward_fees(int64_t reward_fees) { return SetField(30, reward_fees); }
  const flatbuffers::String *created_at() const { return GetPointer<const flatbuffers::String *>(32); }
  flatbuffers::String *mutable_created_at() { return GetPointer<flatbuffers::String *>(32); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<int32_t>(verifier, 4 /* version */) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 6 /* mrkl_root */) &&
           verifier.Verify(mrkl_root()) &&
           VerifyField<uint32_t>(verifier, 8 /* timestamp */) &&
           VerifyField<uint32_t>(verifier, 10 /* curr_max_timestamp */) &&
           VerifyField<uint32_t>(verifier, 12 /* bits */) &&
           VerifyField<uint32_t>(verifier, 14 /* nonce */) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 16 /* prev_block_hash */) &&
           verifier.Verify(prev_block_hash()) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 18 /* next_block_hash */) &&
           verifier.Verify(next_block_hash()) &&
           VerifyField<int32_t>(verifier, 20 /* size */) &&
           VerifyField<int64_t>(verifier, 22 /* pool_difficulty */) &&
           VerifyField<double>(verifier, 24 /* difficulty */) &&
           VerifyField<uint32_t>(verifier, 26 /* tx_count */) &&
           VerifyField<int64_t>(verifier, 28 /* reward_block */) &&
           VerifyField<int64_t>(verifier, 30 /* reward_fees */) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 32 /* created_at */) &&
           verifier.Verify(created_at()) &&
           verifier.EndTable();
  }
};

struct BlockBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_version(int32_t version) { fbb_.AddElement<int32_t>(4, version, 0); }
  void add_mrkl_root(flatbuffers::Offset<flatbuffers::String> mrkl_root) { fbb_.AddOffset(6, mrkl_root); }
  void add_timestamp(uint32_t timestamp) { fbb_.AddElement<uint32_t>(8, timestamp, 0); }
  void add_curr_max_timestamp(uint32_t curr_max_timestamp) { fbb_.AddElement<uint32_t>(10, curr_max_timestamp, 0); }
  void add_bits(uint32_t bits) { fbb_.AddElement<uint32_t>(12, bits, 0); }
  void add_nonce(uint32_t nonce) { fbb_.AddElement<uint32_t>(14, nonce, 0); }
  void add_prev_block_hash(flatbuffers::Offset<flatbuffers::String> prev_block_hash) { fbb_.AddOffset(16, prev_block_hash); }
  void add_next_block_hash(flatbuffers::Offset<flatbuffers::String> next_block_hash) { fbb_.AddOffset(18, next_block_hash); }
  void add_size(int32_t size) { fbb_.AddElement<int32_t>(20, size, 0); }
  void add_pool_difficulty(int64_t pool_difficulty) { fbb_.AddElement<int64_t>(22, pool_difficulty, 0); }
  void add_difficulty(double difficulty) { fbb_.AddElement<double>(24, difficulty, 0); }
  void add_tx_count(uint32_t tx_count) { fbb_.AddElement<uint32_t>(26, tx_count, 0); }
  void add_reward_block(int64_t reward_block) { fbb_.AddElement<int64_t>(28, reward_block, 0); }
  void add_reward_fees(int64_t reward_fees) { fbb_.AddElement<int64_t>(30, reward_fees, 0); }
  void add_created_at(flatbuffers::Offset<flatbuffers::String> created_at) { fbb_.AddOffset(32, created_at); }
  BlockBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  BlockBuilder &operator=(const BlockBuilder &);
  flatbuffers::Offset<Block> Finish() {
    auto o = flatbuffers::Offset<Block>(fbb_.EndTable(start_, 15));
    return o;
  }
};

inline flatbuffers::Offset<Block> CreateBlock(flatbuffers::FlatBufferBuilder &_fbb,
   int32_t version = 0,
   flatbuffers::Offset<flatbuffers::String> mrkl_root = 0,
   uint32_t timestamp = 0,
   uint32_t curr_max_timestamp = 0,
   uint32_t bits = 0,
   uint32_t nonce = 0,
   flatbuffers::Offset<flatbuffers::String> prev_block_hash = 0,
   flatbuffers::Offset<flatbuffers::String> next_block_hash = 0,
   int32_t size = 0,
   int64_t pool_difficulty = 0,
   double difficulty = 0,
   uint32_t tx_count = 0,
   int64_t reward_block = 0,
   int64_t reward_fees = 0,
   flatbuffers::Offset<flatbuffers::String> created_at = 0) {
  BlockBuilder builder_(_fbb);
  builder_.add_reward_fees(reward_fees);
  builder_.add_reward_block(reward_block);
  builder_.add_difficulty(difficulty);
  builder_.add_pool_difficulty(pool_difficulty);
  builder_.add_created_at(created_at);
  builder_.add_tx_count(tx_count);
  builder_.add_size(size);
  builder_.add_next_block_hash(next_block_hash);
  builder_.add_prev_block_hash(prev_block_hash);
  builder_.add_nonce(nonce);
  builder_.add_bits(bits);
  builder_.add_curr_max_timestamp(curr_max_timestamp);
  builder_.add_timestamp(timestamp);
  builder_.add_mrkl_root(mrkl_root);
  builder_.add_version(version);
  return builder_.Finish();
}

struct BlockTxsHash FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  const flatbuffers::String *hash_str() const { return GetPointer<const flatbuffers::String *>(4); }
  flatbuffers::String *mutable_hash_str() { return GetPointer<flatbuffers::String *>(4); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 4 /* hash_str */) &&
           verifier.Verify(hash_str()) &&
           verifier.EndTable();
  }
};

struct BlockTxsHashBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_hash_str(flatbuffers::Offset<flatbuffers::String> hash_str) { fbb_.AddOffset(4, hash_str); }
  BlockTxsHashBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  BlockTxsHashBuilder &operator=(const BlockTxsHashBuilder &);
  flatbuffers::Offset<BlockTxsHash> Finish() {
    auto o = flatbuffers::Offset<BlockTxsHash>(fbb_.EndTable(start_, 1));
    return o;
  }
};

inline flatbuffers::Offset<BlockTxsHash> CreateBlockTxsHash(flatbuffers::FlatBufferBuilder &_fbb,
   flatbuffers::Offset<flatbuffers::String> hash_str = 0) {
  BlockTxsHashBuilder builder_(_fbb);
  builder_.add_hash_str(hash_str);
  return builder_.Finish();
}

struct AddressTx FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  const flatbuffers::String *tx_hash() const { return GetPointer<const flatbuffers::String *>(4); }
  flatbuffers::String *mutable_tx_hash() { return GetPointer<flatbuffers::String *>(4); }
  int32_t tx_height() const { return GetField<int32_t>(6, -1); }
  bool mutate_tx_height(int32_t tx_height) { return SetField(6, tx_height); }
  int32_t ymd() const { return GetField<int32_t>(8, -1); }
  bool mutate_ymd(int32_t ymd) { return SetField(8, ymd); }
  int64_t balance_diff() const { return GetField<int64_t>(10, 0); }
  bool mutate_balance_diff(int64_t balance_diff) { return SetField(10, balance_diff); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 4 /* tx_hash */) &&
           verifier.Verify(tx_hash()) &&
           VerifyField<int32_t>(verifier, 6 /* tx_height */) &&
           VerifyField<int32_t>(verifier, 8 /* ymd */) &&
           VerifyField<int64_t>(verifier, 10 /* balance_diff */) &&
           verifier.EndTable();
  }
};

struct AddressTxBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_tx_hash(flatbuffers::Offset<flatbuffers::String> tx_hash) { fbb_.AddOffset(4, tx_hash); }
  void add_tx_height(int32_t tx_height) { fbb_.AddElement<int32_t>(6, tx_height, -1); }
  void add_ymd(int32_t ymd) { fbb_.AddElement<int32_t>(8, ymd, -1); }
  void add_balance_diff(int64_t balance_diff) { fbb_.AddElement<int64_t>(10, balance_diff, 0); }
  AddressTxBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  AddressTxBuilder &operator=(const AddressTxBuilder &);
  flatbuffers::Offset<AddressTx> Finish() {
    auto o = flatbuffers::Offset<AddressTx>(fbb_.EndTable(start_, 4));
    return o;
  }
};

inline flatbuffers::Offset<AddressTx> CreateAddressTx(flatbuffers::FlatBufferBuilder &_fbb,
   flatbuffers::Offset<flatbuffers::String> tx_hash = 0,
   int32_t tx_height = -1,
   int32_t ymd = -1,
   int64_t balance_diff = 0) {
  AddressTxBuilder builder_(_fbb);
  builder_.add_balance_diff(balance_diff);
  builder_.add_ymd(ymd);
  builder_.add_tx_height(tx_height);
  builder_.add_tx_hash(tx_hash);
  return builder_.Finish();
}

struct AddressTxIdx FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  int32_t index() const { return GetField<int32_t>(4, 0); }
  bool mutate_index(int32_t index) { return SetField(4, index); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<int32_t>(verifier, 4 /* index */) &&
           verifier.EndTable();
  }
};

struct AddressTxIdxBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_index(int32_t index) { fbb_.AddElement<int32_t>(4, index, 0); }
  AddressTxIdxBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  AddressTxIdxBuilder &operator=(const AddressTxIdxBuilder &);
  flatbuffers::Offset<AddressTxIdx> Finish() {
    auto o = flatbuffers::Offset<AddressTxIdx>(fbb_.EndTable(start_, 1));
    return o;
  }
};

inline flatbuffers::Offset<AddressTxIdx> CreateAddressTxIdx(flatbuffers::FlatBufferBuilder &_fbb,
   int32_t index = 0) {
  AddressTxIdxBuilder builder_(_fbb);
  builder_.add_index(index);
  return builder_.Finish();
}

struct UnspentOutput FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  const flatbuffers::String *tx_hash() const { return GetPointer<const flatbuffers::String *>(4); }
  flatbuffers::String *mutable_tx_hash() { return GetPointer<flatbuffers::String *>(4); }
  int32_t position() const { return GetField<int32_t>(6, 0); }
  bool mutate_position(int32_t position) { return SetField(6, position); }
  int16_t position2() const { return GetField<int16_t>(8, 0); }
  bool mutate_position2(int16_t position2) { return SetField(8, position2); }
  int64_t value() const { return GetField<int64_t>(10, 0); }
  bool mutate_value(int64_t value) { return SetField(10, value); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 4 /* tx_hash */) &&
           verifier.Verify(tx_hash()) &&
           VerifyField<int32_t>(verifier, 6 /* position */) &&
           VerifyField<int16_t>(verifier, 8 /* position2 */) &&
           VerifyField<int64_t>(verifier, 10 /* value */) &&
           verifier.EndTable();
  }
};

struct UnspentOutputBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_tx_hash(flatbuffers::Offset<flatbuffers::String> tx_hash) { fbb_.AddOffset(4, tx_hash); }
  void add_position(int32_t position) { fbb_.AddElement<int32_t>(6, position, 0); }
  void add_position2(int16_t position2) { fbb_.AddElement<int16_t>(8, position2, 0); }
  void add_value(int64_t value) { fbb_.AddElement<int64_t>(10, value, 0); }
  UnspentOutputBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  UnspentOutputBuilder &operator=(const UnspentOutputBuilder &);
  flatbuffers::Offset<UnspentOutput> Finish() {
    auto o = flatbuffers::Offset<UnspentOutput>(fbb_.EndTable(start_, 4));
    return o;
  }
};

inline flatbuffers::Offset<UnspentOutput> CreateUnspentOutput(flatbuffers::FlatBufferBuilder &_fbb,
   flatbuffers::Offset<flatbuffers::String> tx_hash = 0,
   int32_t position = 0,
   int16_t position2 = 0,
   int64_t value = 0) {
  UnspentOutputBuilder builder_(_fbb);
  builder_.add_value(value);
  builder_.add_position(position);
  builder_.add_tx_hash(tx_hash);
  builder_.add_position2(position2);
  return builder_.Finish();
}

struct UnspentOutputIdx FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  int32_t index() const { return GetField<int32_t>(4, 0); }
  bool mutate_index(int32_t index) { return SetField(4, index); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<int32_t>(verifier, 4 /* index */) &&
           verifier.EndTable();
  }
};

struct UnspentOutputIdxBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_index(int32_t index) { fbb_.AddElement<int32_t>(4, index, 0); }
  UnspentOutputIdxBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  UnspentOutputIdxBuilder &operator=(const UnspentOutputIdxBuilder &);
  flatbuffers::Offset<UnspentOutputIdx> Finish() {
    auto o = flatbuffers::Offset<UnspentOutputIdx>(fbb_.EndTable(start_, 1));
    return o;
  }
};

inline flatbuffers::Offset<UnspentOutputIdx> CreateUnspentOutputIdx(flatbuffers::FlatBufferBuilder &_fbb,
   int32_t index = 0) {
  UnspentOutputIdxBuilder builder_(_fbb);
  builder_.add_index(index);
  return builder_.Finish();
}

struct DoubleSpending FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *txs() const { return GetPointer<const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *>(4); }
  flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *mutable_txs() { return GetPointer<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *>(4); }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<flatbuffers::uoffset_t>(verifier, 4 /* txs */) &&
           verifier.Verify(txs()) &&
           verifier.VerifyVectorOfStrings(txs()) &&
           verifier.EndTable();
  }
};

struct DoubleSpendingBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_txs(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> txs) { fbb_.AddOffset(4, txs); }
  DoubleSpendingBuilder(flatbuffers::FlatBufferBuilder &_fbb) : fbb_(_fbb) { start_ = fbb_.StartTable(); }
  DoubleSpendingBuilder &operator=(const DoubleSpendingBuilder &);
  flatbuffers::Offset<DoubleSpending> Finish() {
    auto o = flatbuffers::Offset<DoubleSpending>(fbb_.EndTable(start_, 1));
    return o;
  }
};

inline flatbuffers::Offset<DoubleSpending> CreateDoubleSpending(flatbuffers::FlatBufferBuilder &_fbb,
   flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> txs = 0) {
  DoubleSpendingBuilder builder_(_fbb);
  builder_.add_txs(txs);
  return builder_.Finish();
}

}  // namespace fbe

#endif  // FLATBUFFERS_GENERATED_EXPLORER_FBE_H_
