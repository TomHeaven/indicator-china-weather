/*
 * Copyright (C) 2013 ~ 2019 National University of Defense Technology(NUDT) & Tianjin Kylin Ltd.
 *
 * Authors:
 *  Kobe Lee    lixiang@kylinos.cn/kobe24_lixiang@126.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "weatherworker.h"
#include "automaticlocation.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QEventLoop>
#include <QFile>
#include <QApplication>

#include "preferences.h"
#include "global.h"
using namespace Global;

const QString appKey = "1fdc7621c3404b7b96d6afb8b046fa11";

inline QString readOsInfo()
{
    QString idParse = "DISTRIB_ID=";
    QString releaseParse = "DISTRIB_RELEASE=";
    QString osId;
    QString osRelease;

    QFile file("/etc/lsb-release");
    if (!file.open(QFile::ReadOnly)) {
        qCritical() << QString("open lsb-release file failed");
        return QString("distro=ukylin&version_os=18.04");
    }

    QByteArray content = file.readAll();
    file.close();
    QTextStream stream(&content, QIODevice::ReadOnly);
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.startsWith(idParse)) {
            osId = line.remove(0, idParse.size());
        }
        else if (line.startsWith(releaseParse)) {
            osRelease = line.remove(0, releaseParse.size());
        }
    }

    return QString("distro=%1&version_os=%2").arg(osId).arg(osRelease);
}

WeatherWorker::WeatherWorker(QObject *parent) :
    QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished, this, [] (QNetworkReply *reply) {
        reply->deleteLater();
    });

    m_automatic = new AutomaticLocation(this);
    connect(m_automatic, &AutomaticLocation::automaticLocationFinished, this, &WeatherWorker::setAutomaticCity);
}

WeatherWorker::~WeatherWorker()
{
    m_networkManager->deleteLater();
}

void WeatherWorker::startAutoLocationTask()
{
    m_automatic->start();
}

bool WeatherWorker::isNetWorkSettingsGood()
{
    //判断网络是否有连接，不一定能上网
    QNetworkConfigurationManager mgr;
    return mgr.isOnline();
}

void WeatherWorker::netWorkOnlineOrNot()
{
    //http://service.ubuntukylin.com:8001/weather/pingnetwork/
    QHostInfo::lookupHost("www.baidu.com", this, SLOT(networkLookedUp(QHostInfo)));
}

void WeatherWorker::networkLookedUp(const QHostInfo &host)
{
    if(host.error() != QHostInfo::NoError) {
        //qDebug() << "test network failed, errorCode:" << host.error();
        emit this->nofityNetworkStatus(host.errorString());
    }
    else {
        //qDebug() << "test network success, the server's ip:" << host.addresses().first().toString();
        emit this->nofityNetworkStatus("OK");
    }
}

void WeatherWorker::refreshObserveWeatherData(const QString &cityId)
{
    if (cityId.isEmpty()) {
        emit responseFailure(0);
        return;
    }

    QString forecastUrls[2] = {QString("https://devapi.qweather.com/v7/weather/now?location=%1&key=%2").arg(cityId, appKey),
                               QString("https://devapi.qweather.com/v7/air/now?location=%1&key=%2").arg(cityId, appKey)};
    for(int i = 0; i < 2; ++i) {
        QNetworkRequest request;
        request.setUrl(forecastUrls[i]);
        //request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);//Qt5.6 for redirect
        QNetworkReply *reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, &WeatherWorker::onWeatherObserveReply);
    }
}

void WeatherWorker::refreshForecastWeatherData(const QString &cityId)
{
    if (cityId.isEmpty()) {
        emit responseFailure(0);
        return;
    }

    //heweather_forecast
    QString forecastUrls[2] = {QString("https://devapi.qweather.com/v7/weather/3d?location=%1&key=%2").arg(cityId, appKey),
                               QString("https://devapi.qweather.com/v7/indices/1d?location=%1&key=%2&type=0").arg(cityId, appKey)};

    for(int i = 0; i < 2; ++i) {
        QNetworkReply *reply = m_networkManager->get(QNetworkRequest(forecastUrls[i]));
        connect(reply, &QNetworkReply::finished, this, &WeatherWorker::onWeatherForecastReply);
    }
}

void WeatherWorker::requestPingBackWeatherServer()
{
    // QNetworkReply *reply = m_networkManager->get(QNetworkRequest(QString("http://service.ubuntukylin.com:8001/weather/pinginformation/")));
    QNetworkReply *reply = m_networkManager->get(QNetworkRequest(QString("https://devapi.qweather.com")));
    connect(reply, &QNetworkReply::finished, this, [=] () {
        QNetworkReply *m_reply = qobject_cast<QNetworkReply*>(sender());
        int statusCode = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(m_reply->error() != QNetworkReply::NoError || statusCode != 200) {//200 is normal status
            qDebug() << "pingback request error:" << m_reply->error() << ", statusCode=" << statusCode;
            emit responseFailure(statusCode);
            return;
        }

        QByteArray ba = m_reply->readAll();
        m_reply->close();
        m_reply->deleteLater();

        QJsonParseError err;
        QJsonDocument jsonDocument = QJsonDocument::fromJson(ba, &err);
        if (err.error != QJsonParseError::NoError) {// Json type error
            qDebug() << "Json type error";
            return;
        }
        if (jsonDocument.isNull() || jsonDocument.isEmpty()) {
            qDebug() << "Json null or empty!";
            return;
        }

        QJsonObject jsonObject = jsonDocument.object();
        if (jsonObject.isEmpty() || jsonObject.size() == 0) {
            return;
        }
        if (jsonObject.contains("info")) {
            QString notifyInfo = jsonObject.value("info").toString();
            if (!notifyInfo.isEmpty() && !notifyInfo.isNull()) {
                emit requestDiplayServerNotify(notifyInfo);
            }
        }
    });
}

void WeatherWorker::requestPostHostInfoToWeatherServer()
{
    QString osInfo = readOsInfo();
    QString hostInfo = QString("%1&version_weather=%2&city=%3").arg(osInfo).arg(qApp->applicationVersion()).arg(m_preferences->m_currentCity);
    this->m_hostInfoParameters = hostInfo;

    QByteArray parameters = hostInfo.toUtf8();
    QNetworkRequest request;
    // request.setUrl(QUrl("http://service.ubuntukylin.com:8001/weather/pingbackmain"));
    request.setUrl(QUrl("https://devapi.qweather.com"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::ContentLengthHeader, parameters.length());
    QNetworkReply *reply = m_networkManager->post(request, parameters);//QNetworkReply *reply = m_networkManager->post(QNetworkRequest(url), parameters);
    connect(reply, &QNetworkReply::finished, this, &WeatherWorker::onPingBackPostReply);
}

bool WeatherWorker::AccessDedirectUrl(const QString &redirectUrl, WeatherType weatherType)
{
    if (redirectUrl.isEmpty())
        return false;

    QNetworkRequest request;
    QString url;
    url = redirectUrl;
    request.setUrl(QUrl(url));

    QNetworkReply *reply = m_networkManager->get(request);

    switch (weatherType) {
    case WeatherType::Type_Observe:
        connect(reply, &QNetworkReply::finished, this, &WeatherWorker::onWeatherObserveReply);
        break;
    case WeatherType::Type_Forecast:
        connect(reply, &QNetworkReply::finished, this, &WeatherWorker::onWeatherForecastReply);
        break;
    default:
        break;
    }

    return true;
}

void WeatherWorker::AccessDedirectUrlWithPost(const QString &redirectUrl)
{
    if (redirectUrl.isEmpty())
        return;

    QNetworkRequest request;
    QString url;
    url = redirectUrl;
    QByteArray parameters = this->m_hostInfoParameters.toUtf8();
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::ContentLengthHeader, parameters.length());
    QNetworkReply *reply = m_networkManager->post(request, parameters);
    connect(reply, &QNetworkReply::finished, this, &WeatherWorker::onPingBackPostReply);
}

QString WeatherWorker::convertObserveCode(QString con_code) {
    int code = con_code.toInt();
    if (code - code/100*100 >= 50) {
        return QString::number(code-50) + 'n';
    } else
        return con_code;
}

QString WeatherWorker::convertForecastCode(QString con_code) {
    int code = con_code.toInt();
    if (code - code/100*100 >= 50) {
        return QString::number(code-50);
    } else
        return con_code;
}

void WeatherWorker::onWeatherObserveReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode == 301 || statusCode == 302) {//redirect
        bool redirection = false;
        QVariant redirectionUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        //qDebug() << "Weather: redirectionUrl=" << redirectionUrl.toString();
        redirection = AccessDedirectUrl(redirectionUrl.toString(), WeatherType::Type_Observe);//AccessDedirectUrl(reply->rawHeader("Location"));
        reply->close();
        reply->deleteLater();
        if (!redirection) {
            emit responseFailure(statusCode);
        }
        return;
    }
    else if (statusCode == 400) {
        qDebug() << "Weather: Network error (HTTP400/Bad Request)";
        emit responseFailure(statusCode);
        return;
    }
    else if (statusCode == 403) {
        qDebug() << "Weather: Username or password invalid (permission denied)";
        emit responseFailure(statusCode);
        return;
    }
    else if (statusCode == 200) {
        // 200 is normal status
        qDebug() << "Weather: status 200";
    }
    else {
        emit responseFailure(statusCode);
        return;
    }

    if(reply->error() != QNetworkReply::NoError) {
        //qDebug() << "weather request error:" << reply->error() << ", statusCode=" << statusCode;
        emit responseFailure(statusCode);
        return;
    }

    QByteArray ba = reply->readAll();
    //QString reply_content = QString::fromUtf8(ba);
    reply->close();
    reply->deleteLater();
    qDebug() << "weather observe size: " << ba.size();

    QJsonParseError err;
    QJsonDocument jsonDocument = QJsonDocument::fromJson(ba, &err);
    if (err.error != QJsonParseError::NoError) {// Json type error
        qDebug() << "Json type error";
        emit responseFailure(0);
        return;
    }
    if (jsonDocument.isNull() || jsonDocument.isEmpty()) {
        qDebug() << "Json null or empty!";
        emit responseFailure(0);
        return;
    }

    QJsonObject jsonObject = jsonDocument.object();
    //qDebug() << "jsonObject" << jsonObject;
    if (jsonObject.isEmpty() || jsonObject.size() == 0) {
        emit responseFailure(0);
        return;
    }

//    qDebug() << "json" << jsonObject;

    if (jsonObject.contains("now"))
    {
        QJsonObject mainObj = jsonObject.value("now").toObject();
        if (mainObj.isEmpty() || mainObj.size() == 0) {
            emit responseFailure(0);
            return;
        }

        if (mainObj.contains("pm10")) {
            // "now":{"pubTime":"2022-09-12T13:00+08:00","aqi":"88","level":"2","category":"良","primary":"O3","pm10":"60","pm2p5":"40","no2":"6","so2":"8","co":"0.5","o3":"190"}
            QJsonObject airObj = mainObj;
            if (!airObj.isEmpty() && airObj.size() > 0) {
                m_preferences->air.aqi = airObj.value("aqi").toString();
                m_preferences->air.qlty = airObj.value("category").toString();
                m_preferences->air.main = airObj.value("primary").toString();
                m_preferences->air.pm25 = airObj.value("pm2p5").toString();
                m_preferences->air.pm10 = airObj.value("pm10").toString();
                m_preferences->air.no2 = airObj.value("no2").toString();
                m_preferences->air.so2 = airObj.value("so2").toString();
                m_preferences->air.co = airObj.value("co").toString();
                m_preferences->air.o3 = airObj.value("o3").toString();

                m_preferences->weather.air = QString("%1(%2)").arg(airObj.value("aqi").toString()).arg(airObj.value("category").toString());
            }
        }
        if (mainObj.contains("feelsLike")) {
            // parse json
            // {"code":"200","updateTime":"2022-09-12T13:32+08:00","fxLink":"http://hfx.link/3ef1",
            //"now":{"obsTime":"2022-09-12T13:23+08:00","temp":"34","feelsLike":"34","icon":"100","text":"晴",
            //"wind360":"294","windDir":"西北风","windScale":"2","windSpeed":"11","humidity":"38","precip":"0.0",
            //"pressure":"1008","vis":"21","cloud":"4","dew":"17"},
            //"refer":{"sources":["QWeather","NMC","ECMWF"],"license":["no commercial use"]}}
            QJsonObject weatherObj = mainObj;
            if (!weatherObj.isEmpty() && weatherObj.size() > 0) {
                m_preferences->air.id = weatherObj.value("obsTime").toString();//如果有weather，则给id赋值

                m_preferences->weather.id = weatherObj.value("obsTime").toString();
                m_preferences->weather.city = weatherObj.value("location").toString();
                m_preferences->weather.updatetime = weatherObj.value("obsTime").toString();
                m_preferences->weather.cloud = weatherObj.value("cloud").toString();
                // TODO: 研究 convertObserveCode 的 DEBUG
                m_preferences->weather.cond_code = convertForecastCode(weatherObj.value("icon").toString());
                m_preferences->weather.cond_txt = weatherObj.value("text").toString();
                m_preferences->weather.fl = weatherObj.value("feelsLike").toString();
                m_preferences->weather.hum = weatherObj.value("humidity").toString();
                m_preferences->weather.pcpn = weatherObj.value("precip").toString(); // TODO: confirm this to be pcpn
                m_preferences->weather.pres = weatherObj.value("pressure").toString();
                m_preferences->weather.tmp = weatherObj.value("temp").toString();
                m_preferences->weather.vis = weatherObj.value("vis").toString();
                m_preferences->weather.wind_deg = weatherObj.value("wind360").toString();
                m_preferences->weather.wind_dir = weatherObj.value("windDir").toString();
                m_preferences->weather.wind_sc = weatherObj.value("windScale").toString();
                m_preferences->weather.wind_spd = weatherObj.value("windSpeed").toString();
            }
        }

        emit this->observeDataRefreshed(m_preferences->weather);
    }
}

void WeatherWorker::onWeatherForecastReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode == 301 || statusCode == 302) {//redirect
        bool redirection = false;
        QVariant redirectionUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        //qDebug() << "Forecast: redirectionUrl=" << redirectionUrl.toString();
        redirection = AccessDedirectUrl(redirectionUrl.toString(), WeatherType::Type_Forecast);//AccessDedirectUrl(reply->rawHeader("Location"));
        reply->close();
        reply->deleteLater();
        if (!redirection) {
            emit responseFailure(statusCode);
        }
        return;
    }
    else if (statusCode == 400) {
        qDebug() << "Forecast: Network error (HTTP400/Bad Request)";
        emit responseFailure(statusCode);
        return;
    }
    else if (statusCode == 403) {
        qDebug() << "Forecast: Username or password invalid (permission denied)";
        emit responseFailure(statusCode);
        return;
    }
    else if (statusCode == 200) {
        // 200 is normal status
    }
    else {
        emit responseFailure(statusCode);
        return;
    }

    if(reply->error() != QNetworkReply::NoError) {
        //qDebug() << "weather forecast request error:" << reply->error() << ", statusCode=" << statusCode;
        emit responseFailure(statusCode);
        return;
    }


    QByteArray ba = reply->readAll();
    //QString reply_content = QString::fromUtf8(ba);
    reply->close();
    reply->deleteLater();
    // qDebug() << "weather forecast size: " << ba.size();

    QJsonParseError err;
    QJsonDocument jsonDocument = QJsonDocument::fromJson(ba, &err);
    if (err.error != QJsonParseError::NoError) {// Json type error
        qDebug() << "Json type error";
        emit responseFailure(0);
        return;
    }
    if (jsonDocument.isNull() || jsonDocument.isEmpty()) {
        qDebug() << "Json null or empty!";
        emit responseFailure(0);
        return;
    }

    QJsonObject jsonObject = jsonDocument.object();
    // qDebug() << "forecast json" << jsonObject;
    if (jsonObject.isEmpty() || jsonObject.size() == 0) {
        emit responseFailure(0);
        return;
    }
    {
        QJsonArray mainObj = jsonObject.value("daily").toArray();
        if (mainObj.isEmpty() || mainObj.size() == 0) {
            emit responseFailure(0);
            return;
        }

        QList<ForecastWeather> forecastDatas;
        QJsonArray forecastObjects = mainObj;
        // qDebug() << "forecastObj2"<< " " <<forecastObjects.size() << " " <<  jsonObject.value("daily").toArray();
        if (forecastObjects.size() == 3) {
            for(int i = 0; i < 3; ++i) {
                QJsonObject forecastObj = forecastObjects.at(i).toObject();
                if (!forecastObj.isEmpty() && forecastObj.size() > 0) {
                    // {"fxDate":"2022-09-12","sunrise":"06:11","sunset":"18:37","moonrise":"19:59","moonset":"08:47","moonPhase":"亏凸月","moonPhaseIcon":"805",
                    //"tempMax":"36","tempMin":"24","iconDay":"100","textDay":"晴","iconNight":"150","textNight":"晴","wind360Day":"0","windDirDay":"北风","windScaleDay":"1-2",
                    //"windSpeedDay":"3","wind360Night":"0","windDirNight":"北风","windScaleNight":"1-2","windSpeedNight":"3","humidity":"56","precip":"0.0","pressure":"1003",
                    //"vis":"25","cloud":"20","uvIndex":"9"}
                    ForecastWeather forecast;
                    forecast.forcast_date = forecastObj.value("fxDate").toString();
                    forecast.cond_code_d =  convertForecastCode(forecastObj.value("iconDay").toString());
                    forecast.cond_code_n = convertForecastCode(forecastObj.value("iconNight").toString());
                    forecast.cond_txt_d = forecastObj.value("textDay").toString();
                    forecast.cond_txt_n = forecastObj.value("textNight").toString();
                    forecast.hum = forecastObj.value("humidity").toString();
                    forecast.mr_ms = forecastObj.value("moonrise").toString()+ " " + forecastObj.value("moonset").toString();
                    forecast.pcpn = forecastObj.value("precip").toString();
                    //forecast.pop = forecastObj.value("pop0").toString();
                    forecast.pres = forecastObj.value("pressure").toString();
                    forecast.sr_ss = forecastObj.value("sunrise").toString() + " " + forecastObj.value("sunset").toString();
                    forecast.tmp_max = forecastObj.value("tempMax").toString();
                    forecast.tmp_min = forecastObj.value("tempMin").toString();
                    forecast.uv_index = forecastObj.value("uvIndex").toString();
                    forecast.vis = forecastObj.value("vis").toString();
                    forecast.wind_deg = forecastObj.value("wind360Day").toString();
                    forecast.wind_dir = forecastObj.value("windDirDay").toString();
                    forecast.wind_sc = forecastObj.value("windScaleDay").toString();
                    forecast.wind_spd = forecastObj.value("windSpeedDay").toString();
                    switch (i) {
                        case 0:
                          m_preferences->forecast0 = forecast;
                          break;
                        case 1:
                          m_preferences->forecast1 = forecast;
                          break;
                        case 2:
                          m_preferences->forecast2 = forecast;
                          break;
                        default:
                          break;
                    }
                    forecastDatas.append(forecast);
                }
            }
        } else {
            //sport_brf 运动指数
            QJsonObject lifestyleObj = forecastObjects.at(0).toObject();
            m_preferences->lifestyle.sport_brf = lifestyleObj.value("category").toString();
            m_preferences->lifestyle.sport_txt = lifestyleObj.value("text").toString();
            //cw_brf 洗车指数
            lifestyleObj = forecastObjects.at(1).toObject();
            m_preferences->lifestyle.cw_brf = lifestyleObj.value("category").toString();
            m_preferences->lifestyle.cw_txt = lifestyleObj.value("text").toString();
            //drsg_brf 穿衣指数
            lifestyleObj = forecastObjects.at(2).toObject();
            m_preferences->lifestyle.drsg_brf = lifestyleObj.value("category").toString();
            m_preferences->lifestyle.drsg_txt = lifestyleObj.value("text").toString();
            // uv_brf 紫外线指数
            lifestyleObj = forecastObjects.at(4).toObject();
            m_preferences->lifestyle.uv_brf = lifestyleObj.value("category").toString();
            m_preferences->lifestyle.uv_txt = lifestyleObj.value("text").toString();
                //trav_brf 旅游指数
            lifestyleObj = forecastObjects.at(5).toObject();
            m_preferences->lifestyle.trav_brf = lifestyleObj.value("category").toString();
            m_preferences->lifestyle.trav_txt = lifestyleObj.value("text").toString();
                //comf_brf  舒适度指数
            lifestyleObj = forecastObjects.at(7).toObject();
            m_preferences->lifestyle.comf_brf = lifestyleObj.value("category").toString();
            m_preferences->lifestyle.comf_txt = lifestyleObj.value("text").toString();
            //flu_brf 感冒指数
            lifestyleObj = forecastObjects.at(8).toObject();
            m_preferences->lifestyle.flu_brf = lifestyleObj.value("category").toString();
            m_preferences->lifestyle.flu_txt = lifestyleObj.value("text").toString();
            //air_brf 空气污染扩散条件指数
            lifestyleObj = forecastObjects.at(9).toObject();
            m_preferences->lifestyle.air_brf = lifestyleObj.value("category").toString();
            m_preferences->lifestyle.air_txt = lifestyleObj.value("text").toString();
        }
        
        emit this->forecastDataRefreshed(forecastDatas, m_preferences->lifestyle);
    }
}

void WeatherWorker::onPingBackPostReply()
{
    QNetworkReply *m_reply = qobject_cast<QNetworkReply*>(sender());
    int statusCode = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(m_reply->error() != QNetworkReply::NoError || statusCode != 200) {//200 is normal status
        //qDebug() << "post host info request error:" << m_reply->error() << ", statusCode=" << statusCode;
        if (statusCode == 301 || statusCode == 302) {//redirect
            QVariant redirectionUrl = m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
            //qDebug() << "pingback redirectionUrl=" << redirectionUrl.toString();
            AccessDedirectUrlWithPost(redirectionUrl.toString());
            m_reply->close();
            m_reply->deleteLater();
        }
        return;
    }

    //QByteArray ba = m_reply->readAll();
    m_reply->close();
    m_reply->deleteLater();
    //QString reply_content = QString::fromUtf8(ba);
    //qDebug() << "return size: " << ba.size() << reply_content;
}

/*  http://www.heweather.com/documents/status-code  */
QString WeatherWorker::getErrorCodeDescription(QString errorCode)
{
    if ("ok" == errorCode) {
        return "数据正常";
    }
    else if ("invalid key" == errorCode) {
        return "错误的key，请检查你的key是否输入以及是否输入有误";
    }
    else if ("unknown location" == errorCode) {
        return "未知或错误城市/地区";
    }
    else if ("no data for this location" == errorCode) {
        return "该城市/地区没有你所请求的数据";
    }
    else if ("no more requests" == errorCode) {
        return "超过访问次数，需要等到当月最后一天24点（免费用户为当天24点）后进行访问次数的重置或升级你的访问量";
    }
    else if ("param invalid" == errorCode) {
        return "参数错误，请检查你传递的参数是否正确";
    }
    else if ("too fast" == errorCode) {//http://www.heweather.com/documents/qpm
        return "超过限定的QPM，请参考QPM说明";
    }
    else if ("dead" == errorCode) {//http://www.heweather.com/contact
        return "无响应或超时，接口服务异常请联系我们";
    }
    else if ("permission denied" == errorCode) {
        return "无访问权限，你没有购买你所访问的这部分服务";
    }
    else if ("sign error" == errorCode) {//http://www.heweather.com/documents/api/s6/sercet-authorization
        return "签名错误，请参考签名算法";
    }
    else {
        return tr("Unknown");
    }
}


void WeatherWorker::setAutomaticCity(const QString &cityName)
{
    bool autoSuccess = false;
    CitySettingData info;

    if (cityName.isEmpty()) {
        emit this->requestAutoLocationData(info, false);
        return;
    }
    //CN101250101,changsha,长沙,CN,China,中国,hunan,湖南,changsha,长沙,28.19409,112.98228,"430101,430100,430000",
    QFile file(":/data/data/china-city-list.csv");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString line = file.readLine();
        line = line.replace("\n", "");
        while (!line.isEmpty()) {
            QStringList resultList = line.split(",");
            if (resultList.length() < 10) {
                line = file.readLine();
                line = line.replace("\n", "");
                continue;
            }

            QString id = resultList.at(0);
            if (!id.startsWith("CN")) {
                line = file.readLine();
                line = line.replace("\n", "");
                continue;
            }

            if (resultList.at(1).compare(cityName, Qt::CaseInsensitive) == 0 ||
                resultList.at(2).compare(cityName, Qt::CaseInsensitive) == 0 ||
                QString(resultList.at(2) + "市").compare(cityName, Qt::CaseInsensitive) == 0 ||
                QString(resultList.at(2) + "区").compare(cityName, Qt::CaseInsensitive) == 0 ||
                QString(resultList.at(2) + "县").compare(cityName, Qt::CaseInsensitive) == 0) {
                id.remove(0, 2);//remove "CN"
                QString name = resultList.at(2);

                if (m_preferences->isCitiesCountOverMax()) {
                    if (m_preferences->isCityIdExist(id)) {
                        //从已有列表中将自动定位的城市设置为默认城市
                        m_preferences->setCurrentCityIdAndName(name);
                    }
                    else {
                        break;
                    }
                }
                else {
                    if (m_preferences->isCityIdExist(id)) {
                        m_preferences->setCurrentCityIdAndName(name);
                    }
                    else {
                        City city;
                        city.id = id;
                        city.name = name;
                        m_preferences->setCurrentCityIdAndName(name);
                        m_preferences->addCityInfoToPref(city);
                        m_preferences->save();
                    }
                }

                info.active = false;
                info.id = id;
                info.name = name;
                info.icon = ":/res/weather_icons/darkgrey/100.png";

                autoSuccess = true;
                break;
            }

            line = file.readLine();
            line = line.replace("\n", "");
        }
        file.close();
    }

    if (autoSuccess) {
        emit this->requestAutoLocationData(info, true);
    }
    else {
        emit this->requestAutoLocationData(info, false);
    }
}
