#include "mtproto/safelink_server.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QDebug>

namespace {

void Check(bool condition, const char *message) {
	if (!condition) {
		qFatal("%s", message);
	}
}

} // namespace

int main(int argc, char **argv) {
	const auto app = QCoreApplication(argc, argv);
	using Server = MTP::SafeLinkServer;
	const auto primary = Server::Primary();
	const auto parsed = Server::Parse(primary.serialize());
	Check(parsed.has_value(), "built-in RSA identity must validate");
	Check(parsed->serialize() == primary.serialize(), "descriptor round trip");
	Check(parsed->address() == "212.189.31.87:2398", "IPv4 display");
	Check(!Server::Parse(QByteArray(16385, ' ')), "response size limit");
	Check(!Server::Parse("[]"), "reject non-object descriptor");
	Check(!Server::Parse("{"), "reject malformed JSON");

	const auto object = QJsonDocument::fromJson(primary.serialize()).object();
	const auto reject = [&](const QString &key, const QJsonValue &value) {
		auto invalid = object;
		invalid.insert(key, value);
		Check(!Server::Parse(QJsonDocument(invalid).toJson()),
			qPrintable("must reject invalid " + key));
	};
	reject("version", 2);
	reject("version", 1.5);
	reject("port", 0);
	reject("port", 65536);
	reject("port", 2398.5);
	reject("dc_id", 0);
	reject("dc_id", 1001);
	reject("name", "");
	reject("name", "Bad\nName");
	reject("name", QString(81, 'x'));
	reject("host", "safelink.chat");
	reject("host", "127.1");
	reject("host", "0x7f000001");
	reject("host", "0.0.0.0");
	reject("host", "::");
	reject("host", "224.0.0.1");
	reject("host", "fe80::1%en0");
	reject("rsa_public_key", primary.publicKey + "garbage");
	reject("rsa_public_key", QString(4097, 'x'));
	reject("server_id", QString(64, '0'));
	reject("rsa_fingerprint", "0000000000000000");

	auto ipv6 = primary;
	ipv6.host = "2001:db8::1";
	Check(Server::Parse(ipv6.serialize()).has_value(), "IPv6 endpoint");
	Check(ipv6.address() == "[2001:db8::1]:2398", "IPv6 display");
	Check(Server::DiscoveryUrl("safelink.chat").toString()
		== "https://safelink.chat/.well-known/safelink-client.json", "domain input");
	Check(Server::DiscoveryUrl(" 212.189.31.87 ").host()
		== "212.189.31.87", "IP input");
	Check(Server::DiscoveryUrl("https://[2001:db8::1]:8443/").port()
		== 8443, "IPv6 discovery");
	for (const auto invalid : {
		"", "http://safelink.chat", "ftp://safelink.chat", "https://",
		"https://user@safelink.chat", "https://user:secret@safelink.chat",
		"https://safelink.chat/path", "https://safelink.chat?secret=1",
		"https://safelink.chat#fragment", "https://safelink.chat:0",
		"https://safelink.chat:65536",
	}) {
		Check(Server::DiscoveryUrl(invalid).isEmpty(), "reject unsafe discovery URL");
	}

	Check(Server::SessionId(primary.id, 0, false) == 0, "unauthorized identity");
	Check(Server::SessionId(primary.id, 42, false) == 42, "legacy account compatibility");
	Check(Server::SessionId(primary.id, 42, true)
		== (42 | 0x0100'0000'0000'0000ULL), "legacy test identity");
	const auto other = QString(64, 'a');
	const auto id = Server::SessionId(other, 42, false);
	Check(id != 42, "cross-server user IDs must not collide");
	Check(id == Server::SessionId(other, 42, false), "stable identity after restart");
	Check(id != Server::SessionId(other, 43, false), "different users");
	Check(id != Server::SessionId(other, 42, true), "separate test environment");
	Check(id != Server::SessionId(QString(64, 'b'), 42, false), "different servers");
	qInfo() << "SafeLink descriptor, discovery URL and account identity tests passed.";
	return 0;
}
