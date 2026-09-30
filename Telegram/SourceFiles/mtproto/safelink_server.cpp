#include "mtproto/safelink_server.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QtEndian>
#include <QtNetwork/QHostAddress>
#include <algorithm>

namespace MTP {

QByteArray SafeLinkServer::serialize() const {
	return QJsonDocument(QJsonObject{
		{ "version", 1 },
		{ "server_id", id },
		{ "name", name },
		{ "host", host },
		{ "port", port },
		{ "dc_id", dcId },
		{ "rsa_public_key", publicKey },
		{ "rsa_fingerprint", fingerprint },
	}).toJson(QJsonDocument::Compact);
}

QString SafeLinkServer::address() const {
	return (host.contains(':') ? '[' + host + ']' : host)
		+ ':' + QString::number(port);
}

std::optional<SafeLinkServer> SafeLinkServer::Parse(const QByteArray &json) {
	if (json.size() > 16384) {
		return std::nullopt;
	}
	const auto object = QJsonDocument::fromJson(json).object();
	auto result = SafeLinkServer{
		.id = object.value("server_id").toString(),
		.name = object.value("name").toString(),
		.host = object.value("host").toString(),
		.publicKey = object.value("rsa_public_key").toString().trimmed(),
		.fingerprint = object.value("rsa_fingerprint").toString(),
		.port = object.value("port").toInt(),
		.dcId = object.value("dc_id").toInt(),
	};
	const auto ip = QHostAddress(result.host);
	if (object.value("version").toInt() != 1
		|| result.port < 1 || result.port > 65535
		|| result.dcId < 1 || result.dcId > 1000
		|| result.name.trimmed().isEmpty() || result.name.size() > 80
		|| ip.isNull() || ip.isMulticast()
		|| ip == QHostAddress::AnyIPv4 || ip == QHostAddress::AnyIPv6
		|| (ip.protocol() == QAbstractSocket::IPv4Protocol
			&& result.host != ip.toString())
		|| !ip.scopeId().isEmpty()
		|| result.publicKey.size() > 4096) {
		return std::nullopt;
	}
	for (const auto ch : result.name) {
		if (!ch.isPrint()) {
			return std::nullopt;
		}
	}
	const auto begin = QByteArray("-----BEGIN RSA PUBLIC KEY-----\n");
	const auto end = QByteArray("-----END RSA PUBLIC KEY-----");
	const auto pem = result.publicKey.toUtf8();
	if (!pem.startsWith(begin) || !pem.endsWith(end)) {
		return std::nullopt;
	}
	auto payload = pem.mid(begin.size(), pem.size() - begin.size() - end.size());
	payload.replace("\n", "").replace("\r", "");
	const auto der = QByteArray::fromBase64(payload);
	// Match the canonical RSA-2048/65537 identity used by iOS and Android.
	// Pin the SHA-256 of the PKCS#1 bytes, not the discovery host name.
	// The Telegram fingerprint hashes the TL-encoded modulus and exponent.
	if (der.toBase64() != payload
		|| der.size() != 270
		|| !der.startsWith(QByteArray::fromHex("3082010a0282010100"))
		|| !der.endsWith(QByteArray::fromHex("0203010001"))
		|| !(uchar(der[9]) & 0x80)
		|| !(uchar(der[264]) & 1)
		|| QCryptographicHash::hash(der, QCryptographicHash::Sha256).toHex()
			!= result.id.toLatin1()) {
		return std::nullopt;
	}
	const auto input = QByteArray::fromHex("fe000100")
		+ der.mid(9, 256) + QByteArray::fromHex("03010001");
	auto fingerprint = QCryptographicHash::hash(input, QCryptographicHash::Sha1).right(8);
	std::reverse(fingerprint.begin(), fingerprint.end());
	if (fingerprint.toHex() != result.fingerprint.toLatin1()) {
		return std::nullopt;
	}
	result.host = ip.toString();
	return result;
}

const SafeLinkServer &SafeLinkServer::Primary() {
	static const auto result = SafeLinkServer{
		.id = QStringLiteral("5cc5b7bffe3c42758a7d4a44c168feb59039b8099ae6a759740d90d5e0393507"),
		.name = QStringLiteral("SafeLink"),
		.host = QStringLiteral("212.189.31.87"),
		.publicKey = QStringLiteral(
			"-----BEGIN RSA PUBLIC KEY-----\n"
			"MIIBCgKCAQEAzmgJTNhh+Rfz1sBBb2htmPUtIJULMB2YRFElh59UbNl7tHe0h73m\n"
			"4wDxMNWd5R/0TInVrXP1XEGwztIdZ56/xKUsm+VvioP+Ohk4vsYK73eArzx4afs4\n"
			"Us1eZhLfEdO6ouAjeuE2oMyoyk9BfDI8vhYU6flAZcHHlAfmFbflkdXvHEqm+PHW\n"
			"76CSmQDJ9yhNoy41cVPvCLw5UKbgu8c/xdIpIIGEk01BJjtCNbiLJKRLjUIFVIlv\n"
			"nsSnrnQwou4I2p90PWjqAQODKiRMscrgYRXj4GO8W9zVibf1ZPzWznmRZVWERWm9\n"
			"Q0XxobndWXPc8Ei4Y2LAp7uA8/iL94nN/wIDAQAB\n"
			"-----END RSA PUBLIC KEY-----"),
		.fingerprint = QStringLiteral("4be27a5bb0fc10c4"),
		.port = 2398,
		.dcId = 2,
	};
	return result;
}

QUrl SafeLinkServer::DiscoveryUrl(const QString &input) {
	const auto text = input.trimmed();
	auto url = QUrl(text.contains("://") ? text : "https://" + text, QUrl::StrictMode);
	if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty()
		|| !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()
		|| (!url.path().isEmpty() && url.path() != "/")
		|| url.port(443) < 1 || url.port(443) > 65535) {
		return {};
	}
	url.setPath(QStringLiteral("/.well-known/safelink-client.json"));
	return url;
}

quint64 SafeLinkServer::SessionId(
		const QString &serverId,
		quint64 userId,
		bool testMode) {
	if (!userId) {
		return 0;
	} else if (serverId == Primary().id) {
		return userId | (testMode ? 0x0100'0000'0000'0000ULL : 0ULL);
	}
	const auto input = serverId.toUtf8() + ':' + QByteArray::number(userId)
		+ (testMode ? ":test" : ":production");
	const auto hash = QCryptographicHash::hash(input, QCryptographicHash::Sha256);
	return qFromBigEndian<quint64>(hash.constData()) | 0x8000'0000'0000'0000ULL;
}

} // namespace MTP
