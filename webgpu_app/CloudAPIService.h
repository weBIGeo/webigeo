#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <vector>

namespace webgpu_app {

struct CloudTime {
    QString label; // e.g., "2026-01-21 12:00"
    QString path;  // e.g., "/2026012112_000/"
};

class CloudAPIService : public QObject {
    Q_OBJECT
public:
    explicit CloudAPIService(QObject* parent = nullptr);

    void fetchAvailableTimes();
    const std::vector<CloudTime>& availableTimes() const;

signals:
    void availableTimesChanged();

private slots:
    void onFetchFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_networkManager;
    std::vector<CloudTime> m_availableTimes;
};

} // namespace webgpu_app
