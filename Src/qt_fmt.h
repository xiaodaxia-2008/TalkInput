/****************************************************************************
**                                MIT License
**
** Copyright (C) 2019-2022 Klarälvdalens Datakonsult AB, a KDAB Group company,
*info@kdab.com
**
** This file is part of KDToolBox (https://github.com/KDAB/KDToolBox).
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, ** and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice (including the next
*paragraph)
** shall be included in all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF ** CONTRACT, TORT OR OTHERWISE,
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
****************************************************************************/

#pragma once

#include <fmt/core.h>

#include <type_traits>

#include <QDebug>
#include <QString>

template <typename T>
struct fmt::formatter<
    T, char,
    std::void_t<
        std::enable_if_t<
            std::is_base_of_v<QObject, T> || std::is_same_v<T, QString>, void>,
        decltype(std::declval<QDebug &>() << std::declval<const T &>())>> {
    constexpr auto parse(fmt::format_parse_context &ctx)
    {
        auto it = ctx.begin();
        auto end = ctx.end();
        if (it != end && *it != '}') {
            throw fmt::format_error("Only {} is supported");
        }
        return it;
    }

    template <typename FormatContext>
    auto format(const T &t, FormatContext &ctx) const
    {
        // This is really expensive (lots of allocations). Unfortunately
        // there isn't something as easy to do that also performs better.
        //
        // * storing buffer+debug as thread_local has reentrancy issues.
        //   Say you format an object of type A; this calls operator<<(QDebug d,
        //   A a). Inside that, you call d << a.b to stream a subobject of type
        //   B. operator<<(QDebug d, B b) might be implemented itself using fmt,
        //   e.g. via std::format(~~~, b.c, b.d, b.e). Now if any sub-object
        //   ends up being printed using *this* streaming (via QDebug), it won't
        //   work.
        //
        // * Using QByteArray + QBuffer to avoid the final conversion
        //   to UTF-8 doesn't really help, it'll replace it with a bunch
        //   of transient small allocations (as QDebug will convert each
        //   string streamed into it in UTF-8 and then append to its
        //   internal buffer)

        QString buffer;
        QDebug debug(&buffer);
        debug.noquote().nospace() << t;
        return fmt::format_to(ctx.out(), "{}", buffer.toStdString());
    }
};