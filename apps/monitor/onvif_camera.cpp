#include "onvif_camera.h"

#include <QAuthenticator>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDomDocument>
#include <QEventLoop>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>
#include <algorithm>
#include <memory>
#include <vector>

namespace {
constexpr auto discoveryNs = "http://schemas.xmlsoap.org/ws/2005/04/discovery";
constexpr auto deviceNs = "http://www.onvif.org/ver10/device/wsdl";
constexpr auto mediaNs = "http://www.onvif.org/ver10/media/wsdl";

struct Device {
  QString name;
  QUrl endpoint;
};

QDomElement child(const QDomElement &parent, const QString &name)
{
  for (auto element = parent.firstChildElement(); !element.isNull(); element = element.nextSiblingElement())
    if (element.localName() == name)
      return element;
  return {};
}

QDomDocument parseXml(const QByteArray &bytes)
{
  QDomDocument document;
  if (!document.setContent(bytes, QDomDocument::ParseOption::UseNamespaceProcessing)
      || !document.doctype().name().isEmpty())
    return {};
  return document;
}

QList<Device> discover()
{
  const QString id = QStringLiteral("urn:uuid:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QByteArray probe = QStringLiteral(R"xml(
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"
 xmlns:a="http://schemas.xmlsoap.org/ws/2004/08/addressing"
 xmlns:d="http://schemas.xmlsoap.org/ws/2005/04/discovery"
 xmlns:dn="http://www.onvif.org/ver10/network/wsdl">
 <s:Header><a:MessageID>%1</a:MessageID>
 <a:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To>
 <a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action></s:Header>
 <s:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types></d:Probe></s:Body>
</s:Envelope>)xml").arg(id).toUtf8();

  QList<Device> devices;
  QEventLoop loop;
  std::vector<std::unique_ptr<QUdpSocket>> sockets;
  for (const auto &iface : QNetworkInterface::allInterfaces()) {
    const auto flags = iface.flags();
    if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::CanMulticast)
        || flags.testFlag(QNetworkInterface::IsLoopBack))
      continue;
    for (const auto &entry : iface.addressEntries()) {
      if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
        continue;
      auto socket = std::make_unique<QUdpSocket>();
      if (!socket->bind(entry.ip(), 0))
        continue;
      socket->setMulticastInterface(iface);
      socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
      auto *udp = socket.get();
      QObject::connect(udp, &QUdpSocket::readyRead, &loop, [&, udp] {
        while (udp->hasPendingDatagrams()) {
          const auto packet = udp->receiveDatagram();
          if (devices.size() >= 128 || !packet.isValid())
            continue;
          const auto envelope = parseXml(packet.data()).documentElement();
          if (child(child(envelope, "Header"), "RelatesTo").text().trimmed() != id)
            continue;
          const auto matches = child(child(envelope, "Body"), "ProbeMatches");
          if (matches.namespaceURI() != QLatin1String(discoveryNs))
            continue;
          for (auto match = matches.firstChildElement(); !match.isNull(); match = match.nextSiblingElement()) {
            if (match.localName() != "ProbeMatch" || devices.size() >= 128)
              continue;
            const QString sender = packet.senderAddress().toString();
            const auto addresses = child(match, "XAddrs").text().simplified().split(' ', Qt::SkipEmptyParts);
            for (const auto &address : addresses) {
              QUrl endpoint(address, QUrl::StrictMode);
              if (!endpoint.isValid() || (endpoint.scheme() != "http" && endpoint.scheme() != "https")
                  || endpoint.host().isEmpty() || !endpoint.userInfo().isEmpty())
                continue;
              endpoint.setHost(sender);
              if (std::none_of(devices.cbegin(), devices.cend(), [&](const auto &device) {
                    return device.endpoint == endpoint;
                  })) {
                QString name = sender;
                for (const auto &scope : child(match, "Scopes").text().simplified().split(' ')) {
                  const QString prefix = QStringLiteral("onvif://www.onvif.org/name/");
                  if (scope.startsWith(prefix)) {
                    const auto decoded = QUrl::fromPercentEncoding(scope.mid(prefix.size()).toUtf8()).simplified();
                    if (!decoded.isEmpty())
                      name = decoded.left(80);
                    break;
                  }
                }
                devices.append({name, endpoint});
              }
              break;
            }
          }
        }
      });
      if (udp->writeDatagram(probe, QHostAddress("239.255.255.250"), 3702) >= 0)
        sockets.push_back(std::move(socket));
    }
  }
  if (!sockets.empty()) {
    QTimer::singleShot(2500, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
  }
  return devices;
}

QString xmlText(QString text)
{
  return text.toHtmlEscaped();
}

QDomElement soap(QNetworkAccessManager &network, const QUrl &endpoint, const QString &ns,
                 const QString &operation, const QString &arguments, const QString &user,
                 const QString &password)
{
  QString security;
  if (!user.isEmpty()) {
    const auto nonce = QUuid::createUuid().toRfc4122();
    const auto created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8();
    const auto digest = QCryptographicHash::hash(nonce + created + password.toUtf8(), QCryptographicHash::Sha1);
    security = QStringLiteral(R"xml(
<wsse:Security xmlns:wsse="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd"
 xmlns:wsu="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd">
 <wsse:UsernameToken><wsse:Username>%1</wsse:Username>
 <wsse:Password Type="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest">%2</wsse:Password>
 <wsse:Nonce EncodingType="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary">%3</wsse:Nonce>
 <wsu:Created>%4</wsu:Created></wsse:UsernameToken>
</wsse:Security>)xml").arg(xmlText(user), QString::fromLatin1(digest.toBase64()),
                         QString::fromLatin1(nonce.toBase64()), QString::fromLatin1(created));
  }
  const auto body = QStringLiteral(R"xml(<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"
 xmlns:m="%1" xmlns:tt="http://www.onvif.org/ver10/schema"><s:Header>%2</s:Header>
 <s:Body><m:%3>%4</m:%3></s:Body></s:Envelope>)xml").arg(ns, security, operation, arguments);
  QNetworkRequest request(endpoint);
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/soap+xml; charset=utf-8; action=\"%1/%2\"").arg(ns, operation));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  auto *reply = network.post(request, body.toUtf8());
  reply->setReadBufferSize(1024 * 1024 + 1);
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(reply, &QIODevice::readyRead, reply, [reply] {
    if (reply->bytesAvailable() > 1024 * 1024)
      reply->abort();
  });
  QTimer::singleShot(4000, &loop, &QEventLoop::quit);
  if (!reply->isFinished())
    loop.exec(QEventLoop::ExcludeUserInputEvents);
  const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto bytes = reply->isFinished() && reply->error() == QNetworkReply::NoError
      && status >= 200 && status < 300 ? reply->readAll() : QByteArray{};
  if (!reply->isFinished())
    reply->abort();
  reply->deleteLater();
  const auto envelope = parseXml(bytes).documentElement();
  const auto response = child(child(envelope, "Body"), operation + "Response");
  return response.namespaceURI() == ns ? response : QDomElement{};
}

std::optional<OnvifCamera> getStreams(const Device &device, const QString &user, const QString &password)
{
  QNetworkAccessManager network;
  network.setProxy(QNetworkProxy::NoProxy);
  QObject::connect(&network, &QNetworkAccessManager::authenticationRequired, &network,
                   [&](QNetworkReply *, QAuthenticator *auth) {
    auth->setUser(user);
    auth->setPassword(password);
  });
  const auto capabilities = soap(network, device.endpoint, QLatin1String(deviceNs), "GetCapabilities",
                                 "<m:Category>Media</m:Category>", user, password);
  const auto media = child(child(capabilities, "Capabilities"), "Media");
  QUrl endpoint(child(media, "XAddr").text().trimmed(), QUrl::StrictMode);
  if (!endpoint.isValid() || (endpoint.scheme() != "http" && endpoint.scheme() != "https")
      || endpoint.host().isEmpty() || !endpoint.userInfo().isEmpty()
      || (device.endpoint.scheme() == "https" && endpoint.scheme() != "https"))
    return {};
  endpoint.setHost(device.endpoint.host());

  const auto profiles = soap(network, endpoint, QLatin1String(mediaNs), "GetProfiles", {}, user, password);
  struct Profile { QString token; qint64 pixels; };
  QList<Profile> videoProfiles;
  for (auto profile = profiles.firstChildElement(); !profile.isNull(); profile = profile.nextSiblingElement()) {
    const auto encoder = child(profile, "VideoEncoderConfiguration");
    const auto token = profile.attribute("token");
    if (profile.localName() != "Profiles" || encoder.isNull() || token.isEmpty())
      continue;
    const auto resolution = child(encoder, "Resolution");
    const auto width = child(resolution, "Width").text().toInt();
    const auto height = child(resolution, "Height").text().toInt();
    videoProfiles.append({token, width > 0 && height > 0 ? qint64(width) * height : 0});
  }
  if (videoProfiles.isEmpty())
    return {};
  std::stable_sort(videoProfiles.begin(), videoProfiles.end(), [](const auto &a, const auto &b) {
    return a.pixels > b.pixels;
  });
  const auto stream = [&](const QString &token) -> QString {
    const auto response = soap(network, endpoint, QLatin1String(mediaNs), "GetStreamUri",
        QStringLiteral("<m:StreamSetup><tt:Stream>RTP-Unicast</tt:Stream>"
                       "<tt:Transport><tt:Protocol>RTSP</tt:Protocol></tt:Transport></m:StreamSetup>"
                       "<m:ProfileToken>%1</m:ProfileToken>").arg(xmlText(token)), user, password);
    QUrl url(child(child(response, "MediaUri"), "Uri").text().trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "rtsp" || url.host().isEmpty())
      return {};
    url.setHost(device.endpoint.host());
    if (!user.isEmpty()) {
      url.setUserName(user, QUrl::DecodedMode);
      url.setPassword(password, QUrl::DecodedMode);
    }
    return url.toString(QUrl::FullyEncoded);
  };

  const auto main = stream(videoProfiles.first().token);
  if (main.isEmpty())
    return {};
  const auto sub = videoProfiles.last().token == videoProfiles.first().token
      ? main : stream(videoProfiles.last().token);
  if (sub.isEmpty())
    return {};
  return OnvifCamera{device.name, main, sub};
}
} // namespace

std::optional<OnvifCamera> discoverOnvifCamera(QWidget *parent)
{
  const auto title = QStringLiteral("发现摄像头");
  QLabel searching(QStringLiteral("正在搜索局域网摄像头…"), parent, Qt::Tool);
  searching.setWindowTitle(title);
  searching.show();
  const auto devices = discover();
  searching.hide();
  if (devices.isEmpty()) {
    QMessageBox::information(parent, title, QStringLiteral("未发现摄像头。请检查电脑与摄像头是否在同一局域网，并确认设备已启用 ONVIF 发现。"));
    return {};
  }

  QStringList choices;
  for (qsizetype i = 0; i < devices.size(); ++i) {
    const auto &device = devices[i];
    choices.append(QStringLiteral("%1. %2 (%3)").arg(i + 1).arg(device.name, device.endpoint.host()));
  }
  bool accepted = false;
  const auto choice = QInputDialog::getItem(parent, title, QStringLiteral("选择摄像头"), choices, 0, false, &accepted);
  if (!accepted)
    return {};

  QDialog credentials(parent);
  credentials.setWindowTitle(title);
  QFormLayout form(&credentials);
  QLineEdit user(&credentials);
  QLineEdit password(&credentials);
  password.setEchoMode(QLineEdit::Password);
  form.addRow(new QLabel(QStringLiteral("需要认证时，请填写摄像头的 ONVIF 账号。"), &credentials));
  form.addRow(QStringLiteral("用户名"), &user);
  form.addRow(QStringLiteral("密码"), &password);
  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &credentials);
  form.addRow(&buttons);
  QObject::connect(&buttons, &QDialogButtonBox::accepted, &credentials, &QDialog::accept);
  QObject::connect(&buttons, &QDialogButtonBox::rejected, &credentials, &QDialog::reject);
  while (credentials.exec() == QDialog::Accepted) {
    searching.setText(QStringLiteral("正在读取摄像头码流地址…"));
    searching.adjustSize();
    searching.show();
    const auto result = getStreams(devices[choices.indexOf(choice)], user.text(), password.text());
    searching.hide();
    if (result)
      return result;
    QMessageBox::warning(parent, title, QStringLiteral("无法读取 RTSP 码流。请检查 ONVIF 账号和设备设置，或取消并手动填写码流地址。"));
  }
  return {};
}
