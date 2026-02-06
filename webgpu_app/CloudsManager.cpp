#include "CloudsManager.h"

#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QDebug>

namespace webgpu_app::clouds {

std::string TimeSlot::format_string() const
{
    if (id.isEmpty()) return "invalid";
    return std::format("{:04d}.{:02d}.{:02d} {:02d}+{:02d}", date.year, date.month, date.day, run_hour, step);
}

APIService::APIService(QObject* parent, int poll_interval_msec)
    : QObject(parent)
    , m_network_manager(new QNetworkAccessManager(this))
    , m_poll_timer(new QTimer(this))
{
    m_poll_timer->setInterval(poll_interval_msec);
    m_poll_timer->setSingleShot(false);
    connect(m_poll_timer, &QTimer::timeout, this, &APIService::check_pending_items);
}

const QVector<TimeSlot>& APIService::get_slots() const
{
    return m_slots;
}

const QHash<QString, int>& APIService::get_slots_map() const
{
    return m_id_to_index;
}

TimeSlot APIService::get_slot(const QString& id) const
{
    if (!m_id_to_index.contains(id)) return {};
    return m_slots[m_id_to_index[id]];
}

DateComponents APIService::parse_timestamp_id(const QString& id) const
{
    // ID Format: YYYYMMDDHH (10 chars)
    if (id.length() != 10) return {0, 0, 0, 0};
    return {
        id.mid(0, 4).toInt(),
        id.mid(4, 2).toInt(),
        id.mid(6, 2).toInt(),
        id.mid(8, 2).toInt()
    };
}

void APIService::refresh_manifest()
{
    QNetworkRequest request(QUrl(m_server_url + "/available"));
    QNetworkReply* reply = m_network_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[CloudAPI] Failed to fetch manifest:" << reply->errorString();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;

        m_slots.clear();
        m_id_to_index.clear();
        m_pending_ids.clear();
        m_poll_timer->stop();

        // CHANGED: Parse the "items" array of objects
        QJsonArray items = doc.object()["items"].toArray();

        for (const auto& val : items) {
            QJsonObject item = val.toObject();
            QString id = item["id"].toString();

            if (id.length() != 10) continue;

            TimeSlot slot;
            slot.id = id;
            slot.date = parse_timestamp_id(id);

            // Map Status String to Enum
            QString status_str = item["status"].toString();
            if (status_str == "ready") slot.status = SlotStatus::Ready;
            else if (status_str == "stale") slot.status = SlotStatus::Stale;
            else if (status_str == "pending") slot.status = SlotStatus::Pending;
            else slot.status = SlotStatus::Unknown;

            // Populate Metadata
            slot.run_id = item["run"].toString();
            slot.run_hour = slot.run_id.mid(8,2).toInt();
            slot.step = item["step"].toInt();
            if (slot.status == SlotStatus::Ready || slot.status == SlotStatus::Stale) {
                slot.path = item["path"].toString();
            }

            // Automatic Polling: If manifest says it's pending, track it.
            if (slot.status == SlotStatus::Pending) {
                m_pending_ids.insert(id);
            }

            m_id_to_index[id] = m_slots.size();
            m_slots.push_back(slot);
        }

        // Restart timer if we found pending items
        if (!m_pending_ids.isEmpty()) {
            m_poll_timer->start();
        }

        emit manifest_loaded();
    });
}

void APIService::select_or_refresh_slot(const QString& timestamp_id)
{
    if (!m_id_to_index.contains(timestamp_id)) {
        qWarning() << "[CloudAPI] Invalid ID selected:" << timestamp_id;
        return;
    }

    QUrl url(m_server_url + "/request");
    QUrlQuery query;
    query.addQueryItem("time", timestamp_id);
    url.setQuery(query);

    QNetworkRequest request(url);
    QNetworkReply* reply = m_network_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, timestamp_id]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[CloudAPI] Network error for" << timestamp_id << ":" << reply->errorString();

            // Update UI to Error state
            if (m_id_to_index.contains(timestamp_id)) {
                auto slot = m_slots[m_id_to_index[timestamp_id]];
                slot.status = SlotStatus::Error;
                emit slot_updated(slot);
            }
            return;
        }
        handle_status_response(timestamp_id, reply->readAll());
    });
}

void APIService::handle_status_response(const QString& timestamp_id, const QByteArray& data)
{
    if (!m_id_to_index.contains(timestamp_id)) return;

    TimeSlot& slot = m_slots[m_id_to_index[timestamp_id]];
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();

    QString status_str = obj["status"].toString();
    SlotStatus new_status = SlotStatus::Error;

    if (status_str == "ready") {
        new_status = SlotStatus::Ready;
    } else if (status_str == "stale") {
        new_status = SlotStatus::Stale;
    } else if (status_str == "pending") {
        new_status = SlotStatus::Pending;
    } else {
        // Handle explicit server errors or unknown states
        QString msg = obj["message"].toString();
        qWarning() << "[CloudAPI] Server returned error for" << timestamp_id << ":" << msg;
        new_status = SlotStatus::Error;
    }

    // Update Metadata if data is available
    if (new_status == SlotStatus::Ready || new_status == SlotStatus::Stale) {
        slot.path = obj["path"].toString();
        slot.run_id = obj["run"].toString();
        slot.step = obj["step"].toInt();

        // Data is ready, stop polling this ID
        m_pending_ids.remove(timestamp_id);
    }
    else if (new_status == SlotStatus::Pending) {
        // Start polling this ID
        m_pending_ids.insert(timestamp_id);
    }
    else if (new_status == SlotStatus::Error) {
        // Stop polling on error
        m_pending_ids.remove(timestamp_id);
    }

    slot.status = new_status;
    emit slot_updated(slot);

    // Manage Timer State
    if (m_pending_ids.isEmpty()) {
        m_poll_timer->stop();
    } else if (!m_poll_timer->isActive()) {
        m_poll_timer->start();
    }
}

void APIService::check_pending_items()
{
    if (m_pending_ids.isEmpty()) {
        m_poll_timer->stop();
        return;
    }

    // Copy the set because select_or_refresh_slot might modify the set in the reply handler
    // (though reply is async, so it's technically safe, but good practice).
    QSet<QString> current_pending = m_pending_ids;

    for (const QString& id : current_pending) {
        select_or_refresh_slot(id);
    }
}

void APIService::fetch_shadow_texture(const QString& timestamp_id)
{
    if (!m_id_to_index.contains(timestamp_id)) {
        qWarning() << "[CloudAPI] fetch_shadow_texture called with unknown ID:" << timestamp_id;
        return;
    }

    const TimeSlot& slot = m_slots[m_id_to_index[timestamp_id]];

    if (slot.status != SlotStatus::Ready && slot.status != SlotStatus::Stale) {
        qWarning() << "[CloudAPI] Attempted to fetch texture for non-ready slot:" << timestamp_id;
        return;
    }

    if (slot.path.isEmpty()) {
        qWarning() << "[CloudAPI] Slot URL is empty for:" << timestamp_id;
        return;
    }

    QString full_url = m_server_url + slot.path + "shadow.ktx2";
    QNetworkRequest request(full_url);
    QNetworkReply* reply = m_network_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, &slot]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[CloudAPI] Shadow texture download failed:" << reply->errorString();
            return;
        }
        emit shadow_texture_loaded(slot, reply->readAll());
    });
}

Manager::Manager(QObject* parent) : QObject(parent), m_api_service(std::make_unique<APIService>())
{

    connect(m_api_service.get(), &APIService::shadow_texture_loaded, this, [this](const TimeSlot& slot, const QByteArray& data) {
        if (m_selected_cloud_slot_id == slot.id && slot.status == SlotStatus::Ready) {
            emit shadow_texture_ready(data);
        }
    });

    connect(m_api_service.get(), &APIService::manifest_loaded, this, [this]() {
        const auto& time_slots = m_api_service->get_slots();
        auto current_date = QDateTime::currentDateTimeUtc();
        auto ymd = QCalendar().partsFromDate(current_date.date());
        int hour = current_date.time().hour();
        for (size_t i = 0; i < time_slots.length(); i++) {
            auto& slot = time_slots[i];
            if (slot.date.year == ymd.year && slot.date.month == ymd.month && slot.date.day == ymd.day && slot.date.hour == hour) {
                select_time_slot(slot);
                break;
            }
        }
    });

    connect(m_api_service.get(), &APIService::slot_updated, this, [this](const TimeSlot& slot) {
        if (m_selected_cloud_slot_id == slot.id && slot.status == clouds::SlotStatus::Ready) {
            m_api_service->fetch_shadow_texture(slot.id);
            emit slot_ready(slot);
        }
    });

    m_api_service->refresh_manifest();

}

void Manager::select_time_slot(const TimeSlot& slot)
{
    if (m_selected_cloud_slot_id == slot.id) {
        return;
    }
    m_selected_cloud_slot_id = slot.id;
    m_api_service->select_or_refresh_slot(slot.id);
}

TimeSlot Manager::selected_time_slot() const { return m_api_service->get_slot(m_selected_cloud_slot_id); }

const QVector<TimeSlot>& Manager::get_slots() const { return m_api_service->get_slots(); }

} // namespace webgpu_app