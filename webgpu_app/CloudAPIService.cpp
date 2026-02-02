#include "CloudAPIService.h"

#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

namespace webgpu_app {

CloudAPIService::CloudAPIService(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &CloudAPIService::onFetchFinished);
}

void CloudAPIService::fetchAvailableTimes()
{
    QNetworkRequest request(QUrl("http://127.0.0.1:8000/"));
    m_networkManager->get(request);
}

const std::vector<CloudTime>& CloudAPIService::availableTimes() const
{
    return m_availableTimes;
}

void CloudAPIService::onFetchFinished(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "CloudAPIService error:" << reply->errorString();
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isArray()) {
        qDebug() << "CloudAPIService: Expected JSON array";
        reply->deleteLater();
        return;
    }

    m_availableTimes.clear();
    QJsonArray array = doc.array();
    for (const auto& value : array) {
        if (!value.isObject()) continue;
        QJsonObject obj = value.toObject();

        QString path = obj["path"].toString();
        QJsonArray dateArr = obj["date"].toArray();

        if (dateArr.size() >= 4) {
            int year = dateArr[0].toInt();
            int month = dateArr[1].toInt();
            int day = dateArr[2].toInt();
            int hour = dateArr[3].toInt();

            QString label = QString("%1-%2-%3 %4:00").arg(year).arg(month, 2, 10, QChar('0')).arg(day, 2, 10, QChar('0')).arg(hour, 2, 10, QChar('0'));
            m_availableTimes.push_back({label, path});
        }
    }

    emit availableTimesChanged();
    reply->deleteLater();
}

} // namespace webgpu_app
