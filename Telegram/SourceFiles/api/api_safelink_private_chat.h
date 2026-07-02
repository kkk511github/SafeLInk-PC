/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class ApiWrap;
class ChannelData;
class UserData;

namespace Api {

class SafeLinkPrivateChat final {
public:
	explicit SafeLinkPrivateChat(not_null<ApiWrap*> api);
	~SafeLinkPrivateChat();

	void load(not_null<ChannelData*> channel);
	void reload(not_null<ChannelData*> channel);
	void setForbidden(
		not_null<ChannelData*> channel,
		bool forbidden,
		Fn<void()> done = nullptr,
		Fn<void()> fail = nullptr);

	[[nodiscard]] bool forbidden(not_null<ChannelData*> channel) const;
	[[nodiscard]] rpl::producer<bool> forbiddenValue(
		not_null<ChannelData*> channel) const;

	[[nodiscard]] bool currentUserCanBypass(
		not_null<ChannelData*> channel) const;
	[[nodiscard]] bool isPrivilegedMember(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) const;
	[[nodiscard]] bool blocksPrivateChat(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) const;
	[[nodiscard]] bool canMention(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) const;

private:
	struct State {
		bool loaded = false;
		bool forbidden = false;
	};

	void request(not_null<ChannelData*> channel, bool force);
	void apply(not_null<ChannelData*> channel, bool forbidden);
	[[nodiscard]] State state(not_null<ChannelData*> channel) const;

	MTP::Sender _api;
	mutable rpl::event_stream<not_null<ChannelData*>> _changes;
	base::flat_map<not_null<ChannelData*>, State> _states;
	base::flat_map<not_null<ChannelData*>, mtpRequestId> _loadRequests;
	base::flat_map<not_null<ChannelData*>, mtpRequestId> _saveRequests;

};

} // namespace Api
