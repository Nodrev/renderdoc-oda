/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2019-2020 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#pragma once

#include <stdint.h>
#include <map>
#include "api/replay/rdcarray.h"
#include "spirv_common.h"
#include "spirv_processor.h"

namespace rdcspv
{
class Editor;

struct IdOrWord
{
  constexpr inline IdOrWord() : value(0) {}
  constexpr inline IdOrWord(uint32_t val) : value(val) {}
  inline IdOrWord(Id id) : value(id.value()) {}
  inline operator uint32_t() const { return value; }
  constexpr inline bool operator==(const IdOrWord o) const { return value == o.value; }
  constexpr inline bool operator!=(const IdOrWord o) const { return value != o.value; }
  constexpr inline bool operator<(const IdOrWord o) const { return value < o.value; }
private:
  uint32_t value;
};

// helper in the style of the auto-generated one for GLSL ext insts
struct OpGLSL450
{
  OpGLSL450(IdResultType resultType, IdResult result, Id glsl450, rdcspv::GLSLstd450 inst,
            const rdcarray<IdOrWord> &params)
      : op(OpCode), wordCount(MinWordSize + (uint16_t)params.size())
  {
    this->resultType = resultType;
    this->result = result;
    this->glsl450 = glsl450;
    this->inst = inst;
    this->params.resize(params.size());
    for(size_t i = 0; i < params.size(); i++)
      this->params[i] = params[i];
  }

  operator Operation() const
  {
    rdcarray<uint32_t> words;
    words.push_back(resultType.value());
    words.push_back(result.value());
    words.push_back(glsl450.value());
    words.push_back((uint32_t)inst);
    words.append(params);
    return Operation(OpCode, words);
  }

  static constexpr Op OpCode = Op::ExtInst;
  static constexpr uint16_t MinWordSize = 4U;
  Op op;
  uint16_t wordCount;
  IdResultType resultType;
  IdResult result;
  Id glsl450;
  rdcspv::GLSLstd450 inst;
  rdcarray<uint32_t> params;
};

struct OperationList : public rdcarray<Operation>
{
  // add an operation and return its result id
  Id add(const rdcspv::Operation &op);
};

struct Binding
{
  Binding() = default;
  Binding(uint32_t s, uint32_t b) : set(s), binding(b) {}
  uint32_t set = 0;
  uint32_t binding = ~0U;

  bool operator<(const Binding &o) const
  {
    if(set != o.set)
      return set < o.set;
    return binding < o.binding;
  }

  bool operator!=(const Binding &o) const { return !operator==(o); }
  bool operator==(const Binding &o) const { return set == o.set && binding == o.binding; }
};

template <typename SPIRVType>
using TypeToId = std::pair<SPIRVType, Id>;

template <typename SPIRVType>
using TypeToIds = rdcarray<TypeToId<SPIRVType>>;

class Editor : public Processor
{
public:
  Editor(rdcarray<uint32_t> &spirvWords);
  ~Editor();

  void Prepare();
  void CreateEmpty(uint32_t major, uint32_t minor);

  Id MakeId();

  Id AddOperation(Iter iter, const Operation &op);

  // callbacks to allow us to update our internal structures over changes

  // called before any modifications are made. Removes the operation from internal structures.
  void PreModify(Iter iter) { UnregisterOp(iter); }
  // called after any modifications, re-adds the operation to internal structures with its new
  // properties
  void PostModify(Iter iter) { RegisterOp(iter); }
  // removed an operation and replaces it with nops
  void Remove(Iter iter)
  {
    UnregisterOp(iter);
    iter.nopRemove();
  }

  StorageClass StorageBufferClass() { return m_StorageBufferClass; }
  void DecorateStorageBufferStruct(Id id);
  void SetName(Id id, const rdcstr &name);
  void SetMemberName(Id id, uint32_t member, const rdcstr &name);
  void AddDecoration(const Operation &op);
  void AddCapability(Capability cap);
  void AddExtension(const rdcstr &extension);
  void AddExecutionMode(const Operation &mode);
  Id ImportExtInst(const char *setname);
  Id AddType(const Operation &op);
  Id AddVariable(const Operation &op);
  Id AddConstant(const Operation &op);
  void AddFunction(const OperationList &ops);

  Iter GetID(Id id);
  // the entry point has 'two' opcodes, the entrypoint declaration and the function.
  // This returns the first, GetID returns the second.
  Iter GetEntry(Id id);
  Iter Begin(Section::Type section) { return Iter(m_SPIRV, m_Sections[section].startOffset); }
  Iter End(Section::Type section) { return Iter(m_SPIRV, m_Sections[section].endOffset); }
  // fetches the id of this type. If it exists already the old ID will be returned, otherwise it
  // will be declared and the new ID returned
  template <typename SPIRVType>
  Id DeclareType(const SPIRVType &t)
  {
    std::map<SPIRVType, Id> &table = GetTable<SPIRVType>();

    auto it = table.lower_bound(t);
    if(it != table.end() && it->first == t)
      return it->second;

    Operation decl = MakeDeclaration(t);
    Id id = MakeId();
    decl[1] = id.value();
    AddType(decl);

    table.insert(it, std::pair<SPIRVType, Id>(t, id));

    return id;
  }

  template <typename SPIRVType>
  Id GetType(const SPIRVType &t)
  {
    std::map<SPIRVType, Id> &table = GetTable<SPIRVType>();

    auto it = table.find(t);
    if(it != table.end())
      return it->second;

    return Id();
  }

  template <typename SPIRVType>
  TypeToIds<SPIRVType> GetTypes()
  {
    std::map<SPIRVType, Id> &table = GetTable<SPIRVType>();

    TypeToIds<SPIRVType> ret;

    for(auto it = table.begin(); it != table.end(); ++it)
      ret.push_back(*it);

    return ret;
  }

  template <typename SPIRVType>
  const std::map<SPIRVType, Id> &GetTypeInfo() const
  {
    return GetTable<SPIRVType>();
  }

  Binding GetBinding(Id id) const
  {
    auto it = bindings.find(id);
    if(it == bindings.end())
      return Binding();
    return it->second;
  }

  Id DeclareStructType(const rdcarray<Id> &members);

  // helper for AddConstant
  template <typename T>
  Id AddConstantImmediate(T t)
  {
    Id typeId = DeclareType(scalar<T>());
    rdcarray<uint32_t> words = {typeId.value(), MakeId().value()};

    words.resize(words.size() + sizeof(T) / 4);

    memcpy(&words[2], &t, sizeof(T));

    return AddConstant(Operation(Op::Constant, words));
  }

  template <typename T>
  Id AddSpecConstantImmediate(T t, uint32_t specId)
  {
    Id typeId = DeclareType(scalar<T>());
    rdcarray<uint32_t> words = {typeId.value(), MakeId().value()};

    words.resize(words.size() + sizeof(T) / 4);

    memcpy(&words[2], &t, sizeof(T));

    rdcspv::Id ret = AddConstant(Operation(Op::SpecConstant, words));

    words.clear();
    words.push_back(ret.value());
    words.push_back((uint32_t)rdcspv::Decoration::SpecId);
    words.push_back(specId);

    AddDecoration(Operation(Op::Decorate, words));

    return ret;
  }

private:
  using Processor::Parse;
  inline void addWords(size_t offs, size_t num) { addWords(offs, (int32_t)num); }
  void addWords(size_t offs, int32_t num);

  Operation MakeDeclaration(const Scalar &s);
  Operation MakeDeclaration(const Vector &v);
  Operation MakeDeclaration(const Matrix &m);
  Operation MakeDeclaration(const Pointer &p);
  Operation MakeDeclaration(const Image &i);
  Operation MakeDeclaration(const Sampler &s);
  Operation MakeDeclaration(const SampledImage &s);
  Operation MakeDeclaration(const FunctionType &f);

  virtual void RegisterOp(Iter iter);
  virtual void UnregisterOp(Iter iter);

  std::map<Id, Binding> bindings;

  std::map<Scalar, Id> scalarTypeToId;
  std::map<Vector, Id> vectorTypeToId;
  std::map<Matrix, Id> matrixTypeToId;
  std::map<Pointer, Id> pointerTypeToId;
  std::map<Image, Id> imageTypeToId;
  std::map<Sampler, Id> samplerTypeToId;
  std::map<SampledImage, Id> sampledImageTypeToId;
  std::map<FunctionType, Id> functionTypeToId;

  StorageClass m_StorageBufferClass = rdcspv::StorageClass::Uniform;

  template <typename SPIRVType>
  std::map<SPIRVType, Id> &GetTable();

  template <typename SPIRVType>
  const std::map<SPIRVType, Id> &GetTable() const;

  rdcarray<uint32_t> &m_ExternalSPIRV;
};

/*
inline bool operator<(const OpDecorate &a, const OpDecorate &b)
{
  if(a.target != b.target)
    return a.target < b.target;
  if(a.decoration.value != b.decoration.value)
    return a.decoration.value < b.decoration.value;

  return memcmp(&a.decoration, &b.decoration, sizeof(a.decoration)) < 0;
}

inline bool operator==(const OpDecorate &a, const OpDecorate &b)
{
  return a.target == b.target && !memcmp(&a.decoration, &b.decoration, sizeof(a.decoration));
}
*/

};    // namespace rdcspv
