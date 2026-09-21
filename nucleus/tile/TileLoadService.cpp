/*****************************************************************************
 * Alpine Terrain Builder
 * Copyright (C) 2022 alpinemaps.org
 * Copyright (C) 2024 Gerald Kimmersdorfer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include "TileLoadService.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QtVersionChecks>
#include <atomic>
#include <nucleus/srs.h>
#include <nucleus/utils/lang.h>

using namespace nucleus::tile;

namespace {
// Backing store of totals().
std::atomic<uint64_t> g_total_requests { 0 };
std::atomic<uint64_t> g_total_bytes { 0 };
} // namespace

TileLoadService::Totals TileLoadService::totals() { return { g_total_requests.load(std::memory_order_relaxed), g_total_bytes.load(std::memory_order_relaxed) }; }

TileLoadService::TileLoadService(const QString& base_url, UrlPattern url_pattern, const QString& file_ending, const LoadBalancingTargets& load_balancing_targets)
    : m_network_manager(new QNetworkAccessManager(this))
    , m_base_url(base_url)
    , m_url_pattern(url_pattern)
    , m_file_ending(file_ending)
    , m_load_balancing_targets(load_balancing_targets)
{
}

TileLoadService::~TileLoadService() = default;

void TileLoadService::load(const tile::Id& tile_id) const
{
    QNetworkRequest request(QUrl(build_tile_url(tile_id)));
    request.setTransferTimeout(int(m_transfer_timeout));
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    request.setAttribute(QNetworkRequest::UseCredentialsAttribute, false);
#endif

    QNetworkReply* reply = m_network_manager->get(request);
    m_active[tile_id] = reply;
    g_total_requests.fetch_add(1, std::memory_order_relaxed);

    // Counted as they arrive, so aborted requests still count what they downloaded.
    auto received = std::make_shared<qint64>(0);
    const auto count_bytes = [received](qint64 total_received) {
        if (total_received > *received) {
            g_total_bytes.fetch_add(uint64_t(total_received - *received), std::memory_order_relaxed);
            *received = total_received;
        }
    };
    connect(reply, &QNetworkReply::downloadProgress, [count_bytes](qint64 bytes_received, qint64) { count_bytes(bytes_received); });

    connect(reply, &QNetworkReply::finished, [tile_id, reply, count_bytes, this]() {
        if (m_aborted.erase(reply)) {
            // cancelled by abort(): the caller already gave up on this request, so report nothing.
            reply->deleteLater();
            return;
        }
        if (const auto it = m_active.find(tile_id); it != m_active.end() && it->second == reply)
            m_active.erase(it);

        const auto error = reply->error();
        const auto timestamp = utils::time_since_epoch();
        if (error == QNetworkReply::NoError) {
            auto tile = std::make_shared<QByteArray>(reply->readAll());
            count_bytes(tile->size()); // in case no final progress signal came
            emit load_finished({tile_id, {NetworkInfo::Status::Good, timestamp}, tile});
        } else if (error == QNetworkReply::ContentNotFoundError) {
            auto tile = std::make_shared<QByteArray>();
            emit load_finished({tile_id, {NetworkInfo::Status::NotFound, timestamp}, tile});
        } else {
            //            qDebug() << reply->url() << ": " << error;
            auto tile = std::make_shared<QByteArray>();
            emit load_finished({tile_id, {NetworkInfo::Status::NetworkError, timestamp}, tile});
        }
        reply->deleteLater();
    });
}

void TileLoadService::abort(const tile::Id& tile_id)
{
    const auto it = m_active.find(tile_id);
    if (it == m_active.end())
        return;
    QNetworkReply* reply = it->second;
    m_aborted.insert(reply);
    m_active.erase(it);
    reply->abort(); // finishes the reply, the handler in load() sees it in m_aborted
}

QString TileLoadService::build_tile_url(tile::Id tile_id) const
{
    switch (m_url_pattern) {
    case UrlPattern::ZXY:
    case UrlPattern::ZYX:
        tile_id = tile_id.to(tile::Scheme::Tms);
        break;
    case UrlPattern::ZXY_yPointingSouth:
    case UrlPattern::ZYX_yPointingSouth:
        tile_id = tile_id.to(tile::Scheme::SlippyMap);
        break;
    }

    QString tile_address;
    switch (m_url_pattern) {
    case UrlPattern::ZXY:
    case UrlPattern::ZXY_yPointingSouth:
        tile_address = QString("%1/%2/%3").arg(tile_id.zoom_level).arg(tile_id.coords.x).arg(tile_id.coords.y);
        break;
    case UrlPattern::ZYX:
    case UrlPattern::ZYX_yPointingSouth:
        tile_address = QString("%1/%3/%2").arg(tile_id.zoom_level).arg(tile_id.coords.x).arg(tile_id.coords.y);
        break;
    }
    if (!m_load_balancing_targets.empty()) {
        const unsigned hash = qHash(tile_address) % 1024;
        const auto index = unsigned((float(hash) / 1024.1f) * float(m_load_balancing_targets.size()));
        assert(index < m_load_balancing_targets.size());
        return m_base_url.arg(m_load_balancing_targets[index]) + tile_address + m_file_ending;
    }
    return m_base_url + tile_address + m_file_ending;
}

unsigned int TileLoadService::transfer_timeout() const
{
    return m_transfer_timeout;
}

void TileLoadService::set_transfer_timeout(unsigned int new_transfer_timeout)
{
    assert(new_transfer_timeout < unsigned(std::numeric_limits<int>::max()));
    m_transfer_timeout = new_transfer_timeout;
}

void TileLoadService::set_base_url(const QString& base_url) { m_base_url = base_url; }
