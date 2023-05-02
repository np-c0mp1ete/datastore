#include <array>
#include <cstring>
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

std::optional<value_type> deserialize_u32(const std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(uint32_t))
    {
        DATASTORE_ASSERT(false);
        return std::nullopt;
    }

    uint32_t value;
    memcpy(&value, buffer.data() + pos, sizeof(uint32_t));
    pos += sizeof(uint32_t);

    return value;
}

bool serialize_u32(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<uint32_t>(value))
    {
        DATASTORE_ASSERT(false);
        return false;
    }

    const uint32_t u32 = std::get<uint32_t>(value);
    std::copy_n(reinterpret_cast<const uint8_t*>(&u32), sizeof(uint32_t), std::back_inserter(buffer));
    return true;
}

std::optional<value_type> deserialize_u64(const std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(uint64_t))
    {
        DATASTORE_ASSERT(false);
        return std::nullopt;
    }

    uint64_t value;
    memcpy(&value, buffer.data() + pos, sizeof(uint64_t));
    pos += sizeof(uint64_t);

    return value;
}

bool serialize_u64(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<uint64_t>(value))
    {
        DATASTORE_ASSERT(false);
        return false;
    }

    const uint64_t u64 = std::get<uint64_t>(value);
    std::copy_n(reinterpret_cast<const uint8_t*>(&u64), sizeof(uint64_t), std::back_inserter(buffer));
    return true;
}

std::optional<value_type> deserialize_f32(const std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(float))
    {
        DATASTORE_ASSERT(false);
        return std::nullopt;
    }

    float value;
    memcpy(&value, buffer.data() + pos, sizeof(float));
    pos += sizeof(float);

    return value;
}

bool serialize_f32(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<float>(value))
    {
        DATASTORE_ASSERT(false);
        return false;
    }

    const float f32 = std::get<float>(value);
    std::copy_n(reinterpret_cast<const uint8_t*>(&f32), sizeof(float), std::back_inserter(buffer));
    return true;
}

std::optional<value_type> deserialize_f64(const std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(double))
    {
        DATASTORE_ASSERT(false);
        return std::nullopt;
    }

    double value;
    memcpy(&value, buffer.data() + pos, sizeof(double));
    pos += sizeof(double);

    return value;
}

bool serialize_f64(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<double>(value))
    {
        DATASTORE_ASSERT(false);
        return false;
    }

    const double f64 = std::get<double>(value);
    std::copy_n(reinterpret_cast<const uint8_t*>(&f64), sizeof(double), std::back_inserter(buffer));
    return true;
}

std::optional<value_type> deserialize_str(const std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(uint64_t))
    {
        DATASTORE_ASSERT(false);
        return std::nullopt;
    }

    const std::optional<value_type> opt = deserialize_u64(buffer, pos);
    if (!opt)
    {
        DATASTORE_ASSERT(false);
        return std::nullopt;
    }
    const auto len = static_cast<size_t>(std::get<uint64_t>(opt.value()));

    if (pos >= buffer.size() || buffer.size() - pos < len)
    {
        DATASTORE_ASSERT(false);
        return std::nullopt;
    }

    auto str = std::string(reinterpret_cast<const char*>(buffer.data() + pos), len);
    pos += len;

    return str;
}

bool serialize_str(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<std::string>(value))
    {
        DATASTORE_ASSERT(false);
        return false;
    }

    std::string s = std::get<std::string>(value);

    bool success = serialize_u64(static_cast<uint64_t>(s.size()), buffer);

    const auto it = buffer.insert(buffer.end(), s.begin(), s.end());
    success = success && static_cast<size_t>(buffer.end() - it) == s.length();

    return success;
}

std::optional<value_type> deserialize_bin(const std::vector<uint8_t>& buffer, size_t& pos)
{
    if (pos >= buffer.size() || buffer.size() - pos < sizeof(uint64_t))
    {
        DATASTORE_ASSERT(false);
        return std::nullopt;
    }

    const std::optional<value_type> opt = deserialize_u64(buffer, pos);
    if (!opt)
    {
        DATASTORE_ASSERT(false);
        return std::nullopt;
    }
    const auto len = static_cast<size_t>(std::get<uint64_t>(opt.value()));

    if (pos >= buffer.size() || buffer.size() - pos < len)
    {
        DATASTORE_ASSERT(false);
        return std::nullopt;
    }

    auto blob = binary_blob_t(buffer.data() + pos, buffer.data() + pos + len);
    pos += len;

    return blob;
}

bool serialize_bin(const value_type& value, std::vector<uint8_t>& buffer)
{
    if (!std::holds_alternative<binary_blob_t>(value))
    {
        DATASTORE_ASSERT(false);
        return false;
    }

    binary_blob_t blob = std::get<binary_blob_t>(value);

    bool success = serialize_u64(static_cast<uint64_t>(blob.size()), buffer);

    const auto it = buffer.insert(buffer.end(), blob.begin(), blob.end());
    success = success && static_cast<size_t>(buffer.end() - it) == blob.size();

    return success;
}

constexpr std::array serializers = {
    std::make_tuple(value_kind::u32, deserialize_u32, serialize_u32),
    std::make_tuple(value_kind::u64, deserialize_u64, serialize_u64),
    std::make_tuple(value_kind::f32, deserialize_f32, serialize_f32),
    std::make_tuple(value_kind::f64, deserialize_f64, serialize_f64),
    std::make_tuple(value_kind::str, deserialize_str, serialize_str),
    std::make_tuple(value_kind::bin, deserialize_bin, serialize_bin),
};

static_assert(std::get<value_kind>(serializers[0]) == value_kind::u32);
static_assert(std::get<value_kind>(serializers[1]) == value_kind::u64);
static_assert(std::get<value_kind>(serializers[2]) == value_kind::f32);
static_assert(std::get<value_kind>(serializers[3]) == value_kind::f64);
static_assert(std::get<value_kind>(serializers[4]) == value_kind::str);
static_assert(std::get<value_kind>(serializers[5]) == value_kind::bin);
static_assert(serializers.size() == to_underlying(value_kind::_count));
} // namespace

namespace detail
{
#define DESERIALIZE_OPT(type, var, func)                                                                               \
    opt = func(buffer, pos);                                                                                           \
    if (!opt)                                                                                                          \
        return std::nullopt;                                                                                           \
    auto var = std::get<type>(opt.value());

std::optional<node> serializer::deserialize_node(path_view path, volume::priority_t volume_priority,
                                                 std::vector<uint8_t>& buffer, size_t& pos)
{
    std::optional<value_type> opt;

    DESERIALIZE_OPT(std::string, name, deserialize_str)

    node n(path + name, volume_priority);

    DESERIALIZE_OPT(uint64_t, values_count, deserialize_u64)
    for (size_t i = 0; i < values_count; ++i)
    {
        DESERIALIZE_OPT(std::string, value_name, deserialize_str)
        DESERIALIZE_OPT(uint64_t, type, deserialize_u64)

        opt = std::get<1>(serializers[static_cast<size_t>(type)])(buffer, pos);
        if (!opt)
            return std::nullopt;
        value_type value = opt.value();
        attr a(value_name, std::move(value));
        n.values_.assign_or_insert_with_limit(value_name, std::move(a), node::max_num_values);
    }

    DESERIALIZE_OPT(uint64_t, subnodes_count, deserialize_u64)
    for (size_t i = 0; i < subnodes_count; ++i)
    {
        std::optional<node> child = deserialize_node(n.path(), volume_priority, buffer, pos);
        if (!child)
            return std::nullopt;

        std::string subnode_name = std::string(child->name());
        auto [subnode, success] = n.subnodes_.find_or_insert_with_limit(
            std::move(subnode_name), std::make_shared<node>(std::move(child.value())), node::max_num_subnodes);

        if (!success)
            return std::nullopt;
    }

    return n;
}

bool serializer::serialize_node(const node& n, std::vector<uint8_t>& buffer)
{
    bool success = true;

    success = success && serialize_str(std::string(n.name()), buffer);

    const size_t values_size_pos = buffer.size();
    success = success && serialize_u64(static_cast<uint64_t>(0), buffer);

    uint64_t num_values = 0;
    n.for_each_value([&](const attr& a) {
        success = success && serialize_str(std::string(a.name()), buffer);
        success = success && serialize_u64(static_cast<uint64_t>(a.get_value_kind().value()), buffer);
        success =
            success && std::get<2>(serializers[static_cast<uint64_t>(a.get_value_kind().value())])(a.value(), buffer);
        num_values++;
    });
    std::copy_n(reinterpret_cast<uint8_t*>(&num_values), sizeof(uint64_t), buffer.data() + values_size_pos);

    const size_t subnodes_size_pos = buffer.size();
    success = success && serialize_u64(static_cast<uint64_t>(0), buffer);

    uint64_t num_subnodes = 0;
    n.for_each_subnode([&](const std::shared_ptr<node>& subnode) {
        if (subnode->deleted())
            return;
        success = success && serialize_node(*subnode, buffer);
        num_subnodes++;
    });
    std::copy_n(reinterpret_cast<uint8_t*>(&num_subnodes), sizeof(uint64_t), buffer.data() + subnodes_size_pos);

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

    DESERIALIZE_OPT(uint32_t, priority, deserialize_u32)

    volume vol("root", static_cast<volume::priority_t>(priority));

    std::optional<node> root_opt = deserialize_node("", static_cast<volume::priority_t>(priority), buffer, pos);
    if (!root_opt)
        return std::nullopt;
    vol.root_ = std::make_shared<node>(std::move(root_opt.value()));

    if (pos != buffer.size())
        return std::nullopt;

    return vol;
}

bool serializer::serialize_volume(volume& vol, std::vector<uint8_t>& buffer)
{
    bool success = true;

    success = success && serialize_bin(volume::signature, buffer);
    success = success && serialize_u32(static_cast<uint32_t>(endian::native), buffer);
    success = success && serialize_u32(static_cast<uint32_t>(vol.priority()), buffer);
    success = success && serialize_node(*vol.root(), buffer);

    return success;
}
} // namespace detail

volume::volume(path_view root_name, priority_t priority)
    : priority_(priority),
      root_(new node(std::move(root_name), priority))
{
}

bool volume::save(const std::filesystem::path& filepath)
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
