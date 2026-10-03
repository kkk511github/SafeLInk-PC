/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_signup.h"

#include "boxes/abstract_box.h"
#include "intro/intro_widget.h"
#include "core/file_utilities.h"
#include "ui/boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "ui/controls/userpic_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "styles/style_intro.h"
#include "styles/style_userpic_button.h"

namespace Intro {
namespace details {

SignupWidget::SignupWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _photo(
	this,
	data->controller,
	Ui::UserpicButton::Role::ChoosePhoto,
	st::defaultUserpicButton)
, _first(this, st::introSignupName, tr::lng_signup_firstname())
, _last(this, st::introSignupName, tr::lng_signup_lastname())
, _invite(this, st::introRegistrationInvite, tr::lng_safelink_registration_invite())
, _invertOrder(langFirstNameGoesSecond()) {
	_photo->showCustomOnChosen();

	Lang::Updated(
	) | rpl::on_next([=] {
		refreshLang();
	}, lifetime());

	if (_invertOrder) {
		setTabOrder(_last, _first);
	} else {
		setTabOrder(_first, _last);
	}

	setErrorCentered(true);
	setTabOrder(_invertOrder ? _first : _last, _invite);

	setTitleText(tr::lng_signup_title());
	setDescriptionText(tr::lng_signup_desc());
	setMouseTracking(true);
}

void SignupWidget::finishInit() {
	showTerms();
}

void SignupWidget::refreshLang() {
	_invertOrder = langFirstNameGoesSecond();
	if (_invertOrder) {
		setTabOrder(_last, _first);
	} else {
		setTabOrder(_first, _last);
	}
	updateControlsGeometry();
}

void SignupWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void SignupWidget::updateControlsGeometry() {
	auto photoRight = contentLeft() + st::introNextButton.width;
	auto photoTop = contentTop() + st::introPhotoTop;
	_photo->moveToLeft(photoRight - _photo->width(), photoTop);

	auto firstTop = contentTop() + st::introStepFieldTop;
	auto secondTop = firstTop + st::introName.heightMin + st::introPhoneTop;
	_invite->resizeToWidth(_first->width() + st::introSignupNameGap + _last->width());
	_invite->moveToLeft(contentLeft(), secondTop);
	const auto secondLeft = contentLeft() + st::introSignupName.width + st::introSignupNameGap;
	if (_invertOrder) {
		_last->moveToLeft(contentLeft(), firstTop);
		_first->moveToLeft(secondLeft, firstTop);
	} else {
		_first->moveToLeft(contentLeft(), firstTop);
		_last->moveToLeft(secondLeft, firstTop);
	}
}

void SignupWidget::setInnerFocus() {
	if (_invertOrder || _last->hasFocus()) {
		_last->setFocusFast();
	} else {
		_first->setFocusFast();
	}
}

void SignupWidget::activate() {
	Step::activate();
	refreshInvitePolicy();
	_first->show();
	_last->show();
	_invite->show();
	_photo->show();
	setInnerFocus();
}

void SignupWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
	api().request(base::take(_invitePolicyRequest)).cancel();
}

void SignupWidget::refreshInvitePolicy() {
	api().request(base::take(_invitePolicyRequest)).cancel();
	_invitePolicyRequest = api().request(MTPhelp_GetAppConfig(
		MTP_int(0)
	)).done([=](const MTPhelp_AppConfig &result) {
		_invitePolicyRequest = 0;
		if (result.type() != mtpc_help_appConfig) {
			return;
		}
		const auto &config = result.c_help_appConfig().vconfig();
		if (config.type() != mtpc_jsonObject) {
			return;
		}
		auto required = false;
		for (const auto &item : config.c_jsonObject().vvalue().v) {
			item.match([&](const MTPDjsonObjectValue &entry) {
				if (qs(entry.vkey()) == u"safelink_registration_invite_required"_q
					&& entry.vvalue().type() == mtpc_jsonBool) {
					required = mtpIsTrue(entry.vvalue().c_jsonBool().vvalue());
				}
			});
		}
		_invite->setPlaceholder(required
			? tr::lng_safelink_registration_invite_mandatory_hint()
			: tr::lng_safelink_registration_invite_optional_hint());
	}).fail([=] {
		_invitePolicyRequest = 0;
	}).send();
}

void SignupWidget::nameSubmitDone(const MTPauth_Authorization &result) {
	finish(result);
}

void SignupWidget::nameSubmitFail(const MTP::Error &error) {
	_sentRequest = 0;
	if (MTP::IsFloodError(error)) {
		showError(tr::lng_flood_error());
		if (_invertOrder) {
			_first->setFocus();
		} else {
			_last->setFocus();
		}
		return;
	}

	const auto &err = error.type();
	if (err == u"INVITE_CODE_REQUIRED"_q || err == u"INVITE_CODE_INVALID"_q) {
		refreshInvitePolicy();
		showError(err == u"INVITE_CODE_REQUIRED"_q ? tr::lng_safelink_registration_invite_required() : tr::lng_safelink_registration_invite_invalid());
		_invite->showError();
		_invite->setFocus();
	} else if (err == u"PHONE_NUMBER_FLOOD"_q) {
		Ui::show(Ui::MakeInformBox(tr::lng_error_phone_flood()));
	} else if (err == u"PHONE_NUMBER_INVALID"_q
		|| err == u"PHONE_NUMBER_BANNED"_q
		|| err == u"PHONE_CODE_EXPIRED"_q
		|| err == u"PHONE_CODE_EMPTY"_q
		|| err == u"PHONE_CODE_INVALID"_q
		|| err == u"PHONE_NUMBER_OCCUPIED"_q) {
		goBack();
	} else if (err == "FIRSTNAME_INVALID") {
		showError(tr::lng_bad_name());
		_first->setFocus();
	} else if (err == "LASTNAME_INVALID") {
		showError(tr::lng_bad_name());
		_last->setFocus();
	} else if (!MTP::IgnoreError(error)) {
		showError(rpl::single(err));
		if (_invertOrder) {
			_last->setFocus();
		} else {
			_first->setFocus();
		}
	}
}

void SignupWidget::submit() {
	if (_sentRequest) {
		return;
	}
	if (_invertOrder) {
		if ((_last->hasFocus() || _last->getLastText().trimmed().length()) && !_first->getLastText().trimmed().length()) {
			_first->setFocus();
			return;
		} else if (!_last->getLastText().trimmed().length()) {
			_last->setFocus();
			return;
		}
	} else {
		if ((_first->hasFocus() || _first->getLastText().trimmed().length()) && !_last->getLastText().trimmed().length()) {
			_last->setFocus();
			return;
		} else if (!_first->getLastText().trimmed().length()) {
			_first->setFocus();
			return;
		}
	}

	const auto send = [&] {
		hideError();

		_firstName = _first->getLastText().trimmed();
		_lastName = _last->getLastText().trimmed();
		auto hash = getData()->phoneHash;
		const auto invite = _invite->getLastText().trimmed();
		if (!invite.isEmpty()) { hash += ":safelink-invite:" + invite.toUtf8(); }
		_sentRequest = api().request(MTPauth_SignUp(
			MTP_flags(0),
			MTP_string(getData()->phone),
			MTP_bytes(hash),
			MTP_string(_firstName),
			MTP_string(_lastName)
		)).done([=](const MTPauth_Authorization &result) {
			nameSubmitDone(result);
		}).fail([=](const MTP::Error &error) {
			nameSubmitFail(error);
		}).handleFloodErrors().send();
	};
	if (_termsAccepted
		|| getData()->termsLock.text.text.isEmpty()
		|| !getData()->termsLock.popup) {
		send();
	} else {
		acceptTerms(crl::guard(this, [=] {
			_termsAccepted = true;
			send();
		}));
	}
}

rpl::producer<QString> SignupWidget::nextButtonText() const {
	return tr::lng_intro_finish();
}

} // namespace details
} // namespace Intro
