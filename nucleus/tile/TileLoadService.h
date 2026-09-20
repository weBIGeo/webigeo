/*****************************************************************************
 * Alpine Terrain Builder
 * Copyright (C) 2022 alpinemaps.org
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

#pragma once

#include <memory>
#include <QObject>
#include <unordered_set>
#include "constants.h"
#include "types.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace nucleus::tile {

class TileLoadService : public QObject {
    Q_OBJECT
public:
    enum class UrlPattern {
        ZXY,
        ZYX, // y=0 is southern most tile
        ZXY_yPointingSouth,
        ZYX_yPointingSouth // y=0 is the northern most tile
    };
    using LoadBalancingTargets = std::vector<QString>;

    TileLoadService(const QString& base_url, UrlPattern url_pattern, const QString& file_ending, const LoadBalancingTargets& load_balancing_targets = {});
    ~TileLoadService() override;
    [[nodiscard]] QString build_tile_url(tile::Id tile_id) const;

    [[nodiscard]] unsigned int transfer_timeout() const;
    void set_transfer_timeout(unsigned int new_transfer_timeout);

    void set_base_url(const QString& base_url);

public slots:
    void load(const tile::Id& tile_id) const;
    /// Cancels the in-flight request for tile_id. A cancelled request emits no load_finished. No-op for unknown or already finished ids.
    void abort(const tile::Id& tile_id);

signals:
    void load_finished(Data tile) const;

private:
    unsigned m_transfer_timeout = tile::constants::default_network_timeout;
    // Declared before m_network_manager on purpose: members are destroyed in reverse order, and destroying the manager
    // may finish replies whose handlers touch these containers.
    mutable tile::IdMap<QNetworkReply*> m_active; // in-flight reply per tile id
    mutable std::unordered_set<QNetworkReply*> m_aborted; // replies cancelled by abort(); a timeout is also reported as OperationCanceledError, so the error code can't tell them apart
    std::shared_ptr<QNetworkAccessManager> m_network_manager;
    QString m_base_url;
    UrlPattern m_url_pattern;
    QString m_file_ending;
    LoadBalancingTargets m_load_balancing_targets;
};
}
