#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <optional>

namespace MTP {

struct SafeLinkServer {
	QString id;
	QString name;
	QString host;
	QString publicKey;
	QString fingerprint;
	int port = 0;
	int dcId = 0;

	[[nodiscard]] QByteArray serialize() const;
	[[nodiscard]] QString address() const;
	[[nodiscard]] static std::optional<SafeLinkServer> Parse(
		const QByteArray &json);
	[[nodiscard]] static const SafeLinkServer &Primary();
	[[nodiscard]] static QUrl DiscoveryUrl(const QString &input);
	[[nodiscard]] static quint64 SessionId(
		const QString &serverId,
		quint64 userId,
		bool testMode);
};

} // namespace MTP
