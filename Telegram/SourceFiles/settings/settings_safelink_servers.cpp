#include "settings/settings_safelink_servers.h"

#include "boxes/premium_limits_box.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "settings/sections/settings_information.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Settings {
namespace {

struct SavedServer {
	MTP::SafeLinkServer server;
	QUrl discovery;
};

QString ServersPath() {
	return cWorkingDir() + u"tdata/safelink-servers.json"_q;
}

bool SameConnection(const MTP::SafeLinkServer &a, const MTP::SafeLinkServer &b) {
	return a.id == b.id && a.host == b.host && a.port == b.port
		&& a.dcId == b.dcId && a.fingerprint == b.fingerprint;
}

bool CanAppend(const std::vector<SavedServer> &list, const SavedServer &entry) {
	for (const auto &known : list) {
		if ((known.server.id == entry.server.id
			|| (known.server.host == entry.server.host
				&& known.server.port == entry.server.port)
			|| (!entry.discovery.isEmpty() && known.discovery == entry.discovery))
			&& !SameConnection(known.server, entry.server)) {
			return false;
		}
	}
	return true;
}

std::optional<std::vector<SavedServer>> ReadServers() {
	auto result = std::vector<SavedServer>();
	auto file = QFile(ServersPath());
	if (file.exists()) {
		if (!file.open(QIODevice::ReadOnly) || file.size() > 262144) {
			return std::nullopt;
		}
		const auto document = QJsonDocument::fromJson(file.readAll());
		if (!document.isArray() || document.array().size() > 32) {
			return std::nullopt;
		}
		for (const auto value : document.array()) {
			const auto object = value.toObject();
			const auto descriptor = QJsonDocument(object.value("server").toObject());
			const auto server = MTP::SafeLinkServer::Parse(descriptor.toJson());
			const auto discovery = QUrl(object.value("discovery").toString());
			if (!server || (!discovery.isEmpty()
				&& (discovery.scheme() != u"https"_q || discovery.host().isEmpty()))) {
				return std::nullopt;
			}
			const auto entry = SavedServer{ *server, discovery };
			if (!CanAppend(result, entry)) {
				return std::nullopt;
			}
			if (ranges::contains(result, server->id, [](const SavedServer &item) {
				return item.server.id;
			})) {
				return std::nullopt;
			}
			result.push_back(entry);
		}
	}
	const auto append = [&](const MTP::SafeLinkServer &server) {
		if (!ranges::contains(result, server.id, [](const SavedServer &item) {
			return item.server.id;
		})) {
			result.push_back({ server, {} });
		}
	};
	append(MTP::SafeLinkServer::Primary());
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		const auto server = MTP::SafeLinkServer::Parse(
			account->mtp().config().serverBinding());
		if (server) {
			if (!CanAppend(result, { *server, {} })) {
				return std::nullopt;
			}
			append(*server);
		}
	}
	return result;
}

bool SaveServer(const SavedServer &entry) {
	auto list = ReadServers();
	if (!list || !CanAppend(*list, entry)) {
		return false;
	}
	auto found = false;
	for (auto &known : *list) {
		if (known.server.id == entry.server.id) {
			known = entry;
			found = true;
			break;
		}
	}
	if (!found) {
		if (list->size() >= 32) {
			return false;
		}
		list->push_back(entry);
	}
	auto array = QJsonArray();
	for (const auto &known : *list) {
		array.push_back(QJsonObject{
			{ "server", QJsonDocument::fromJson(known.server.serialize()).object() },
			{ "discovery", known.discovery.toString() },
		});
	}
	QDir().mkpath(cWorkingDir() + u"tdata"_q);
	auto file = QSaveFile(ServersPath());
	const auto bytes = QJsonDocument(array).toJson(QJsonDocument::Compact);
	return file.open(QIODevice::WriteOnly)
		&& file.write(bytes) == bytes.size() && file.commit();
}

void AddAccount(
		not_null<Window::SessionController*> controller,
		MTP::SafeLinkServer server) {
	controller->window().preventOrInvoke(crl::guard(controller, [=] {
		auto &domain = Core::App().domain();
		domain.removeRedundantAccounts();
		Core::App().setActivePrimaryWindow(&controller->window());
		if (!domain.addServerAccount(server)) {
			controller->show(Box(AccountsLimitBox, &controller->session()));
		}
	}));
}

} // namespace

void ShowAddServerBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_safelink_add_server());
		box->setWidth(st::boxWideWidth);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box, st::settingsDeviceName, tr::lng_safelink_server_address()));
		field->setMaxLength(512);
		box->setFocusCallback([=] { field->setFocusFast(); });
		struct State {
			~State() {
				if (reply) {
					QObject::disconnect(reply.data(), nullptr, nullptr, nullptr);
					reply->abort();
				}
			}

			QNetworkAccessManager network;
			QPointer<QNetworkReply> reply;
			QByteArray data;
			rpl::variable<QString> status;
		};
		const auto state = box->lifetime().make_state<State>();
		box->addRow(object_ptr<Ui::FlatLabel>(
			box, state->status.value(), st::boxLabel));
		const auto fetch = [=] {
			if (state->reply) {
				return;
			}
			const auto url = MTP::SafeLinkServer::DiscoveryUrl(field->getLastText());
			if (url.isEmpty()) {
				field->showError();
				state->status = tr::lng_safelink_address_error(tr::now);
				return;
			}
			auto request = QNetworkRequest(url);
			request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
				QNetworkRequest::ManualRedirectPolicy);
			request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
				QNetworkRequest::Manual);
			request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
				QNetworkRequest::Manual);
			request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,
				QNetworkRequest::Manual);
			request.setRawHeader("Accept", "application/json");
			state->data.clear();
			state->status = tr::lng_safelink_discovering(tr::now);
			field->setDisabled(true);
			const auto reply = state->network.get(request);
			state->reply = reply;
			reply->setReadBufferSize(16385);
			QTimer::singleShot(20000, reply, [=] { reply->abort(); });
			QObject::connect(reply, &QNetworkReply::readyRead, reply, [=] {
				state->data += reply->readAll();
				if (state->data.size() > 16384) {
					reply->abort();
				}
			});
			QObject::connect(reply, &QNetworkReply::finished, box, [=] {
				state->reply = nullptr;
				field->setDisabled(false);
				reply->deleteLater();
				state->data += reply->readAll();
				const auto server = MTP::SafeLinkServer::Parse(state->data);
				if (reply->error() != QNetworkReply::NoError
					|| reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200
					|| reply->header(QNetworkRequest::ContentTypeHeader).toString()
						.section(';', 0, 0).trimmed() != u"application/json"_q
					|| !server) {
					state->status = tr::lng_safelink_discovery_error(tr::now);
					return;
				}
				const auto entry = SavedServer{ *server, url };
				const auto saved = ReadServers();
				if (!saved || !CanAppend(*saved, entry)) {
					state->status = tr::lng_safelink_server_conflict(tr::now);
					return;
				}
				state->status = QString();
				controller->show(Ui::MakeConfirmBox({
					.text = rpl::single(server->name + '\n' + url.host()
						+ '\n' + server->address() + u"\n\nSHA-256\n"_q
						+ server->id.left(32) + '\n' + server->id.mid(32)
						+ '\n' + tr::lng_safelink_trust_server(tr::now)),
					.confirmed = crl::guard(box, [=](Fn<void()> close) {
						if (!SaveServer(entry)) {
							close();
							state->status = tr::lng_safelink_server_save_error(tr::now);
							return;
						}
						close();
						box->closeBox();
						ShowServersBox(controller);
					}),
					.confirmText = tr::lng_safelink_confirm_server(),
				}), Ui::LayerOption::KeepOther);
			});
		};
		box->addButton(tr::lng_safelink_discover_server(), fetch);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		field->submits() | rpl::on_next(fetch, box->lifetime());
	}));
}

void ShowServersBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_safelink_servers_accounts());
		box->setWidth(st::boxWideWidth);
		Core::App().domain().accountsChanges() | rpl::on_next([=] {
			box->closeBox();
		}, box->lifetime());
		for (const auto &[index, account] : Core::App().domain().accounts()) {
			account->sessionChanges() | rpl::on_next([=] {
				box->closeBox();
			}, box->lifetime());
		}
		const auto layout = box->verticalLayout();
		const auto servers = ReadServers();
		if (!servers) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box, tr::lng_safelink_server_save_error(), st::boxLabel));
		} else {
			for (const auto &entry : *servers) {
				const auto server = entry.server;
				const auto active = controller->session().mtp().config().serverId() == server.id;
				Ui::AddSubsectionTitle(layout, rpl::single(server.name
					+ (active ? tr::lng_safelink_current_server(tr::now) : QString())));
				layout->add(object_ptr<Ui::FlatLabel>(layout,
					rpl::single(server.address()), st::boxLabel), st::boxPadding);
				SetupServerAccounts(layout, controller, server.id, [=] {
					box->closeBox();
				});
				const auto button = layout->add(CreateButtonWithIcon(layout,
					tr::lng_menu_add_account(), st::mainMenuAddAccountButton,
					{ &st::settingsIconAdd, IconType::Round, &st::windowBgActive }));
				button->setClickedCallback([=] { AddAccount(controller, server); });
			}
		}
		const auto add = layout->add(CreateButtonWithIcon(layout,
			tr::lng_safelink_add_server(), st::mainMenuAddAccountButton,
			{ &st::settingsIconAdd, IconType::Round, &st::windowBgActive }));
		add->setClickedCallback([=] { ShowAddServerBox(controller); });
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace Settings
