/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_safelink_private_chat.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/type_utils.h"

namespace Api {

SafeLinkPrivateChat::SafeLinkPrivateChat(not_null<ApiWrap*> api)
: _api(&api->instance()) {
}

SafeLinkPrivateChat::~SafeLinkPrivateChat() = default;

void SafeLinkPrivateChat::load(not_null<ChannelData*> channel) {
	request(channel, false);
}

void SafeLinkPrivateChat::reload(not_null<ChannelData*> channel) {
	request(channel, true);
}

void SafeLinkPrivateChat::request(
		not_null<ChannelData*> channel,
		bool force) {
	if (!channel->isMegagroup()
		|| (!force && state(channel).loaded)
		|| _loadRequests.contains(channel)) {
		return;
	}

	const auto requestId = _api.request(
		MTPsafelink_GetGroupPrivateChatForbidden(channel->inputChannel())
	).done([=](const MTPBool &result) {
		_loadRequests.remove(channel);
		apply(channel, mtpIsTrue(result));
	}).fail([=] {
		_loadRequests.remove(channel);
		apply(channel, false);
	}).send();

	_loadRequests.emplace(channel, requestId);
}

void SafeLinkPrivateChat::setForbidden(
		not_null<ChannelData*> channel,
		bool forbidden,
		Fn<void()> done,
		Fn<void()> fail) {
	if (!channel->isMegagroup()) {
		if (fail) {
			fail();
		}
		return;
	}
	if (const auto i = _saveRequests.find(channel); i != end(_saveRequests)) {
		_api.request(i->second).cancel();
		_saveRequests.erase(i);
	}

	const auto previous = state(channel);
	apply(channel, forbidden);

	const auto requestId = _api.request(
		MTPsafelink_ToggleGroupPrivateChatForbidden(
			channel->inputChannel(),
			MTP_bool(forbidden))
	).done([=](const MTPUpdates &result) {
		_saveRequests.remove(channel);
		channel->session().api().applyUpdates(result);
		apply(channel, forbidden);
		if (done) {
			done();
		}
	}).fail([=] {
		_saveRequests.remove(channel);
		if (previous.loaded) {
			apply(channel, previous.forbidden);
		} else {
			_states.remove(channel);
			_changes.fire_copy(channel);
		}
		if (fail) {
			fail();
		}
	}).send();

	_saveRequests.emplace(channel, requestId);
}

bool SafeLinkPrivateChat::forbidden(not_null<ChannelData*> channel) const {
	return channel->isMegagroup() && state(channel).forbidden;
}

rpl::producer<bool> SafeLinkPrivateChat::forbiddenValue(
		not_null<ChannelData*> channel) const {
	return rpl::single(
		forbidden(channel)
	) | rpl::then(
		_changes.events() | rpl::filter([=](not_null<ChannelData*> updated) {
			return updated == channel;
		}) | rpl::map([=] {
			return forbidden(channel);
		}));
}

bool SafeLinkPrivateChat::currentUserCanBypass(
		not_null<ChannelData*> channel) const {
	return !channel->isMegagroup()
		|| channel->amCreator()
		|| channel->hasAdminRights();
}

bool SafeLinkPrivateChat::isPrivilegedMember(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) const {
	if (!channel->isMegagroup() || !channel->mgInfo) {
		return true;
	}
	const auto info = channel->mgInfo.get();
	return (info->creator == user)
		|| channel->isGroupAdmin(user)
		|| info->lastAdmins.contains(user);
}

bool SafeLinkPrivateChat::blocksPrivateChat(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) const {
	return forbidden(channel)
		&& !currentUserCanBypass(channel)
		&& !user->isSelf()
		&& !isPrivilegedMember(channel, user);
}

bool SafeLinkPrivateChat::canMention(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) const {
	return !forbidden(channel)
		|| currentUserCanBypass(channel)
		|| isPrivilegedMember(channel, user);
}

void SafeLinkPrivateChat::apply(
		not_null<ChannelData*> channel,
		bool forbidden) {
	_states[channel] = {
		.loaded = true,
		.forbidden = forbidden,
	};
	_changes.fire_copy(channel);
}

SafeLinkPrivateChat::State SafeLinkPrivateChat::state(
		not_null<ChannelData*> channel) const {
	if (const auto i = _states.find(channel); i != end(_states)) {
		return i->second;
	}
	return {};
}

} // namespace Api
