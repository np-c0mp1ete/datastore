#include <array>
#include <filesystem>
#include <fstream>
#include <tuple>

#include "datastore/node.hpp"
#include "datastore/volume.hpp"

namespace datastore
{
namespace
{
enum class endian
{
#ifdef _WIN32
    little = 0,
    big = 1,
    native = little
#else
    little = __ORDER_LITTLE_ENDIAN__,
    big = __ORDER_BIG_ENDIAN__,
    native = __BYTE_ORDER__
#endif
};

std::optional<value_type> deserialize_u32(std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(uint32_t))
        return std::nullopt;

    uint32_t value = *reinterpret_cast<uint32_t*>(buffer.data() + pos);
    pos += sizeof(uint32_t);
    return value;
}

bool serialize_u32(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<uint32_t>(value))
        return false;

    const uint32_t u32 = std::get<uint32_t>(value);
    std::copy_n(reinterpret_cast<const uint8_t*>(&u32), sizeof(uint32_t), std::back_inserter(buffer));
    return true;
}

std::optional<value_type> deserialize_u64(std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(uint64_t))
        return std::nullopt;

    uint64_t value = *reinterpret_cast<uint64_t*>(buffer.data() + pos);
    pos += sizeof(uint64_t);
    return value;
}

bool serialize_u64(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<uint64_t>(value))
        return false;

    const uint64_t u64 = std::get<uint64_t>(value);
    std::copy_n(reinterpret_cast<const uint8_t*>(&u64), sizeof(uint64_t), std::back_inserter(buffer));
    return true;
}

std::optional<value_type> deserialize_f32(std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(float))
        return std::nullopt;

    float value = *reinterpret_cast<float*>(buffer.data() + pos);
    pos += sizeof(float);
    return value;
}

bool serialize_f32(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<float>(value))
        return false;

    const float f32 = std::get<float>(value);
    std::copy_n(reinterpret_cast<const uint8_t*>(&f32), sizeof(float), std::back_inserter(buffer));
    return true;
}

std::optional<value_type> deserialize_f64(std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(double))
        return std::nullopt;

    double value = *reinterpret_cast<double*>(buffer.data() + pos);
    pos += sizeof(double);
    return value;
}

bool serialize_f64(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<double>(value))
        return false;

    const double f64 = std::get<double>(value);
    std::copy_n(reinterpret_cast<const uint8_t*>(&f64), sizeof(double), std::back_inserter(buffer));
    return true;
}

std::optional<value_type> deserialize_str(std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(uint64_t))
        return std::nullopt;

    const std::optional<value_type> opt = deserialize_u64(buffer, pos);
    if (!opt)
        return std::nullopt;
    const uint64_t len = std::get<uint64_t>(opt.value());

    if (pos >= buffer.size() || buffer.size() - pos < len)
        return std::nullopt;

    auto str = std::string(reinterpret_cast<char*>(buffer.data() + pos), len);
    pos += len;

    return str;
}

bool serialize_str(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<std::string>(value))
        return false;

    std::string s = std::get<std::string>(value);

    bool success = serialize_u64(static_cast<uint64_t>(s.size()), buffer);

    const auto it = buffer.insert(buffer.end(), s.begin(), s.end());
    success = success && static_cast<size_t>(buffer.end() - it) == s.length();

    return success;
}

std::optional<value_type> deserialize_bin(std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(uint64_t))
        return std::nullopt;

    const std::optional<value_type> opt = deserialize_u64(buffer, pos);
    if (!opt)
        return std::nullopt;
    const uint64_t len = std::get<uint64_t>(opt.value());

    if (pos >= buffer.size() || buffer.size() - pos < len)
        return std::nullopt;

    auto blob = binary_blob_t(buffer.data() + pos, buffer.data() + pos + len);
    pos += len;

    return blob;
}

bool serialize_bin(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<binary_blob_t>(value))
        return false;

    binary_blob_t blob = std::get<binary_blob_t>(value);

    bool success = serialize_u64(static_cast<uint64_t>(blob.size()), buffer);

    const auto it = buffer.insert(buffer.end(), blob.begin(), blob.end());
    success = success && static_cast<size_t>(buffer.end() - it) == blob.size();

    return success;
}

std::optional<value_type> deserialize_ref(std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(uint64_t))
        return std::nullopt;

    const std::optional<value_type> opt = deserialize_str(buffer, pos);
    if (!opt)
        return std::nullopt;
    auto& path = std::get<std::string>(opt.value());

    return ref{path};
}

bool serialize_ref(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<ref>(value))
        return false;

    const ref& r = std::get<ref>(value);
    return  serialize_str(r.path, buffer);
}

constexpr std::array serializers = {
    std::make_tuple(value_kind::u32, deserialize_u32, serialize_u32),
    std::make_tuple(value_kind::u64, deserialize_u64, serialize_u64),
    std::make_tuple(value_kind::f32, deserialize_f32, serialize_f32),
    std::make_tuple(value_kind::f64, deserialize_f64, serialize_f64),
    std::make_tuple(value_kind::str, deserialize_str, serialize_str),
    std::make_tuple(value_kind::bin, deserialize_bin, serialize_bin),
    std::make_tuple(value_kind::ref, deserialize_ref, serialize_ref)
};

static_assert(std::get<value_kind>(serializers[0]) == value_kind::u32);
static_assert(std::get<value_kind>(serializers[1]) == value_kind::u64);
static_assert(std::get<value_kind>(serializers[2]) == value_kind::f32);
static_assert(std::get<value_kind>(serializers[3]) == value_kind::f64);
static_assert(std::get<value_kind>(serializers[4]) == value_kind::str);
static_assert(std::get<value_kind>(serializers[5]) == value_kind::bin);
static_assert(std::get<value_kind>(serializers[6]) == value_kind::ref);
static_assert(serializers.size() == to_underlying(value_kind::_count));
}

namespace detail
{
#define DESERIALIZE_OPT(type, var, func)                                                                               \
    opt = func(buffer, pos);                                                                                           \
    if (!opt)                                                                                                          \
        return std::nullopt;                                                                                           \
    auto var = std::get<type>(opt.value());

std::optional<node> serializer::deserialize_node(volume& vol, node* parent, std::vector<uint8_t>& buffer, size_t& pos)
{
    std::optional<value_type> opt;

    DESERIALIZE_OPT(std::string, name, deserialize_str)

    node n(name, &vol, parent);

    DESERIALIZE_OPT(uint64_t, values_count, deserialize_u64)
    for (size_t i = 0; i < values_count; ++i)
    {
        DESERIALIZE_OPT(std::string, value_name, deserialize_str)
        DESERIALIZE_OPT(uint64_t, type, deserialize_u64)

        opt = std::get<1>(serializers[type])(buffer, pos);
        if (!opt)
            return std::nullopt;
        value_type value = opt.value();
        n.values_.emplace(value_name, std::move(value));
    }

    DESERIALIZE_OPT(uint64_t, children_count, deserialize_u64)
    for (size_t i = 0; i < children_count; ++i)
    {
        std::optional<node> child = deserialize_node(vol, &n, buffer, pos);
        if (!child)
            return std::nullopt;

        // child->parent_ = &n;
        auto [it, inserted] = n.subnodes_.emplace(child->name(), std::move(child.value()));

        if (!inserted)
            return std::nullopt;

        // it->second.parent_ = &n;
    }

    return n;
}

bool serializer::serialize_node(node& n, std::vector<uint8_t>& buffer)
{
    bool success = true;

    success = success && serialize_str(n.name_, buffer);
    success = success && serialize_u64(static_cast<uint64_t>(n.values_.size()), buffer);

    for (auto& [value_name, value] : n.values_)
    {
        success = success && serialize_str(value_name, buffer);
        success = success && serialize_u64(static_cast<uint64_t>(value.index()), buffer);
        success = success && std::get<2>(serializers[value.index()])(value, buffer);
    }

    success = success && serialize_u64(static_cast<uint64_t>(n.subnodes_.size()), buffer);

    // TODO: don't serialize deleted nodes
    for (auto& [subnode_name, subnode] : n.subnodes_)
    {
        success = success && serialize_node(subnode, buffer);
    }

    return success;
}

std::optional<volume> serializer::deserialize_volume(std::vector<uint8_t>& buffer)
{
    size_t pos = 0;
    std::optional<value_type> opt;

    DESERIALIZE_OPT(binary_blob_t, signature, deserialize_bin)
    if (signature != volume::signature)
        return std::nullopt;

    DESERIALIZE_OPT(uint32_t, endianness, deserialize_u32)
    if (static_cast<endian>(endianness) != endian::native)
        return std::nullopt;

    DESERIALIZE_OPT(uint64_t, priority, deserialize_u64)

    volume vol(static_cast<volume::priority_t>(priority));

    std::optional<node> root_opt = deserialize_node(vol, nullptr, buffer, pos);
    if (!root_opt)
        return std::nullopt;
    vol.root_ = std::move(root_opt.value());

    if (pos != buffer.size())
        return std::nullopt;

    return vol;
}

// TODO: use ostream instead of buffer. use overloaded operator <<?
bool serializer::serialize_volume(volume& vol, std::vector<uint8_t>& buffer)
{
    bool success = true;

    success = success && serialize_bin(volume::signature, buffer);
    success = success && serialize_u32(static_cast<uint32_t>(endian::native), buffer);
    success = success && serialize_u64(static_cast<uint64_t>(vol.priority()), buffer);
    success = success && serialize_node(*vol.root(), buffer);

    return success;
}
}




volume::volume(priority_t priority) : priority_(priority)
{
}

[[nodiscard]] volume::volume(const volume& other) noexcept : priority_(other.priority_), root_(other.root_)
{
    root_.set_volume(this);
}

[[nodiscard]] volume::volume(volume&& other) noexcept : priority_(other.priority_), root_(std::move(other.root_))
{
    root_.set_volume(this);
}

volume& volume::operator=(const volume& rhs) noexcept
{
    if (this == &rhs)
        return *this;

    priority_ = rhs.priority_;
    root_ = rhs.root_;

    root_.set_volume(this);

    return *this;
}

volume& volume::operator=(volume&& rhs) noexcept
{
    priority_ = rhs.priority_;
    root_ = rhs.root_;

    root_.set_volume(this);

    return *this;
}

bool volume::unload(const std::filesystem::path& filepath)
{
    std::ofstream ofs(filepath, std::ios::binary);

    if (!ofs)
        return false;

    std::vector<uint8_t> buffer;
    buffer.reserve(256);

    detail::serializer s;
    s.serialize_volume(*this, buffer);

    if (!ofs.write(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size())))
        return false;

    ofs.close();

    return true;
}

std::optional<volume> volume::load(const std::filesystem::path& filepath)
{
    if (!std::filesystem::is_regular_file(filepath))
        return std::nullopt;

    std::ifstream ifs(filepath, std::ios::binary | std::ios::ate);

    if (!ifs)
        return std::nullopt;

    const auto end = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    const auto size = static_cast<size_t>(end - ifs.tellg());

    if (size == 0)
        return std::nullopt;

    std::vector<uint8_t> buffer(size);

    if (!ifs.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size())))
        return std::nullopt;

    ifs.close();

    detail::serializer s;
    return s.deserialize_volume(buffer);
}
} // namespace datastore
