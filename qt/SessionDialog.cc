/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "Prefs.h"
#include "Session.h"
#include "SessionDialog.h"

/***
****
***/

void SessionDialog::accept()
{
    prefs_.set(Prefs::SESSION_IS_REMOTE, ui_.remoteSessionRadio->isChecked());
    prefs_.set(Prefs::SESSION_REMOTE_HOST, ui_.hostEdit->text());
    prefs_.set(Prefs::SESSION_REMOTE_PORT, ui_.portSpin->value());
    prefs_.set(Prefs::SESSION_REMOTE_AUTH, ui_.authCheck->isChecked());
    prefs_.set(Prefs::SESSION_REMOTE_USERNAME, ui_.usernameEdit->text());
    prefs_.set(Prefs::SESSION_REMOTE_PASSWORD, ui_.passwordEdit->text());
    session_.restart();
    BaseDialog::accept();
}

void SessionDialog::resensitize()
{
    bool const is_remote = ui_.remoteSessionRadio->isChecked();
    bool const use_auth = ui_.authCheck->isChecked();

    for (QWidget* const w : remote_widgets_)
    {
        w->setEnabled(is_remote);
    }

    for (QWidget* const w : auth_widgets_)
    {
        w->setEnabled(is_remote && use_auth);
    }
}

/***
****
***/

SessionDialog::SessionDialog(Session& session, Prefs& prefs, QWidget* parent) :
    BaseDialog(parent),
    session_(session),
    prefs_(prefs)
{
    ui_.setupUi(this);

    ui_.localSessionRadio->setChecked(!prefs.get<bool>(Prefs::SESSION_IS_REMOTE));
    connect(ui_.localSessionRadio, SIGNAL(toggled(bool)), this, SLOT(resensitize()));

    ui_.remoteSessionRadio->setChecked(prefs.get<bool>(Prefs::SESSION_IS_REMOTE));
    connect(ui_.remoteSessionRadio, SIGNAL(toggled(bool)), this, SLOT(resensitize()));

    ui_.hostEdit->setText(prefs.get<QString>(Prefs::SESSION_REMOTE_HOST));
    remote_widgets_ << ui_.hostLabel << ui_.hostEdit;

    ui_.portSpin->setValue(prefs.get<int>(Prefs::SESSION_REMOTE_PORT));
    remote_widgets_ << ui_.portLabel << ui_.portSpin;

    ui_.authCheck->setChecked(prefs.get<bool>(Prefs::SESSION_REMOTE_AUTH));
    connect(ui_.authCheck, SIGNAL(toggled(bool)), this, SLOT(resensitize()));
    remote_widgets_ << ui_.authCheck;

    ui_.usernameEdit->setText(prefs.get<QString>(Prefs::SESSION_REMOTE_USERNAME));
    auth_widgets_ << ui_.usernameLabel << ui_.usernameEdit;

    ui_.passwordEdit->setText(prefs.get<QString>(Prefs::SESSION_REMOTE_PASSWORD));
    auth_widgets_ << ui_.passwordLabel << ui_.passwordEdit;

    resensitize();
}
