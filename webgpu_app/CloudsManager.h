#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QTimer>


namespace webgpu_app::clouds {
     struct DateComponents {
        int year;
        int month;
        int day;
        int hour;
    };

    enum class SlotStatus {
        Unknown, // Exists in manifest, but haven't checked availability yet
        Pending, // Server is currently generating this (polling active)
        Ready, // Data exists and is valid
        Stale, // Data exists but is old (server is regenerating)
        Error // Request failed or out of window
    };

    inline std::string to_string(SlotStatus status) {
        switch (status) {
        case SlotStatus::Unknown: return "unknown";
        case SlotStatus::Pending: return "pending";
        case SlotStatus::Ready:   return "ready";
        case SlotStatus::Stale:   return "stale";
        case SlotStatus::Error:   return "error";
        default:                  return "invalid status";
        }
    }

    struct TimeSlot {
        QString id; // "2026020412"
        DateComponents date;
        SlotStatus status = SlotStatus::Unknown;

        // Populated when status is Ready/Stale
        QString run_id; // "2026020412"
        int run_hour;
        int step = 0;
        QString path; // "/2026020412_000/"

        [[nodiscard]] std::string format_string() const;
    };

    class APIService : public QObject {
        Q_OBJECT
    public:
        explicit APIService(QObject* parent = nullptr, int poll_interval_msec = 10000);

        // Populates the internal list of slots with "Unknown" status
        void refresh_manifest();

        [[nodiscard]] const QVector<TimeSlot>& get_slots() const;
        [[nodiscard]] const QHash<QString, int>& get_slots_map() const;
        [[nodiscard]] TimeSlot get_slot(const QString& id) const;

        // Call this when the user clicks a time in the UI.
        // If "Unknown", it queries server. If "Pending", it ensures polling is active.
        void select_or_refresh_slot(const QString& timestamp_id);

        // Looks up the slot by ID. If Ready/Stale, fetches the file.
        void fetch_shadow_texture(const QString& timestamp_id);

    signals:
        // Fired when the list size/content changes significantly (full UI rebuild)
        void manifest_loaded();

        // Fired when a specific slot changes status (e.g., Pending -> Ready)
        // UI should only redraw this specific row/item.
        void slot_updated(const TimeSlot& slot);

        // Fired when the actual binary data is downloaded
        void shadow_texture_loaded(const TimeSlot& slot, const QByteArray& data);

    private:
        // Internal helper to parse YYYYMMDDHH
        [[nodiscard]] DateComponents parse_timestamp_id(const QString& id) const;

        // Internal helper to process /request JSON response
        void handle_status_response(const QString& timestamp_id, const QByteArray& data);

        // Polling Logic
        void check_pending_items();

        QNetworkAccessManager* m_network_manager;
        QTimer* m_poll_timer;

        // Data Store
        QVector<TimeSlot> m_slots;
        QHash<QString, int> m_id_to_index; // Fast lookup for m_slots

        // Polling State
        QSet<QString> m_pending_ids; // IDs that we are currently polling

        const QString m_server_url = "http://127.0.0.1:8000";
    };

    class Manager : public QObject {
        Q_OBJECT
    public:
        explicit Manager(QObject* parent = nullptr);

        void select_time_slot(const TimeSlot& slot);

        [[nodiscard]] TimeSlot selected_time_slot() const;

        [[nodiscard]] const QVector<TimeSlot>& get_slots() const;

    signals:
        void slot_ready(const TimeSlot& slot);
        void shadow_texture_ready(const QByteArray& data);

    private:
        std::unique_ptr<APIService> m_api_service;
        QString m_selected_cloud_slot_id = "";
    };
} // namespace

