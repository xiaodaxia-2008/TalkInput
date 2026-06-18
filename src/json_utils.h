#pragma once

#include <QString>
#include <boost/pfr.hpp>
#include <boost/pfr/core_name.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

inline void to_json(nlohmann::json &j, const QString &value)
{
    j = value.toStdString();
}

inline void from_json(const nlohmann::json &j, QString &value)
{
    value = j.is_string() ? QString::fromStdString(j.get<std::string>())
                          : QString();
}

namespace talkinput::json_detail
{

template <typename T>
struct IsStdPair : std::false_type
{
};

template <typename A, typename B>
struct IsStdPair<std::pair<A, B>> : std::true_type
{
};

template <typename T>
struct IsStdArray : std::false_type
{
};

template <typename T, std::size_t N>
struct IsStdArray<std::array<T, N>> : std::true_type
{
};

template <typename T>
using Clean = std::remove_cvref_t<T>;

template <typename T>
concept ReflectableAggregate =
    std::is_aggregate_v<Clean<T>> && !std::is_array_v<Clean<T>> &&
    !std::is_union_v<Clean<T>> && !std::is_arithmetic_v<Clean<T>> &&
    !std::is_enum_v<Clean<T>> && !std::is_same_v<Clean<T>, std::string> &&
    !IsStdPair<Clean<T>>::value && !IsStdArray<Clean<T>>::value &&
    requires { boost::pfr::names_as_array<Clean<T>>(); };

template <ReflectableAggregate T, std::size_t... I>
void aggregateToJson(nlohmann::json &j, const T &value,
                     std::index_sequence<I...>)
{
    j = nlohmann::json::object();
    constexpr auto names = boost::pfr::names_as_array<Clean<T>>();
    ((j[std::string(names[I])] = boost::pfr::get<I>(value)), ...);
}

template <ReflectableAggregate T, std::size_t... I>
void aggregateFromJson(const nlohmann::json &j, T &value,
                       std::index_sequence<I...>)
{
    constexpr auto names = boost::pfr::names_as_array<Clean<T>>();
    auto assign = [&]<std::size_t Index>() {
        const std::string key(names[Index]);
        if (!j.contains(key)) {
            return;
        }
        boost::pfr::get<Index>(value) =
            j.at(key).template get<Clean<decltype(
                boost::pfr::get<Index>(value))>>();
    };
    (assign.template operator()<I>(), ...);
}

} // namespace talkinput::json_detail

namespace nlohmann
{

template <typename T>
struct adl_serializer<
    T, std::enable_if_t<talkinput::json_detail::ReflectableAggregate<T>>>
{
    static void to_json(json &j, const T &value)
    {
        talkinput::json_detail::aggregateToJson(
            j, value,
            std::make_index_sequence<
                boost::pfr::tuple_size_v<talkinput::json_detail::Clean<T>>>{});
    }

    static void from_json(const json &j, T &value)
    {
        if (!j.is_object()) {
            throw std::runtime_error(
                "JSON input must be an object to map onto a struct.");
        }
        talkinput::json_detail::aggregateFromJson(
            j, value,
            std::make_index_sequence<
                boost::pfr::tuple_size_v<talkinput::json_detail::Clean<T>>>{});
    }
};

} // namespace nlohmann
