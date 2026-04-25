/*-----------------------------------------------------------------------------------------------------------------------------------------------------
Библиотека для вывода на экран форм авторизации и регистрации пользователя. На базе Wt. Не используется authWidget из библиотеки в силу его крайней
сложности и интегрированности с Wt::Dbo. Все формы собраны с помощью стандартных виджетов библиотеки Wt.
Работает на Wt 4.10.3.
Взаимодействие с вызывающим Wt::WApplication осуществляется через переопределение виртуальных функций, см. authWidgets.hpp.

Автор - Ярослав Медокс.
-----------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "authwidgets.hpp"
#include <Wt/WApplication.h>
#include <Wt/WLineEdit.h>
#include <Wt/WText.h>
#include <Wt/WPushButton.h>
//#include <Wt/WBreak.h>
#include <Wt/WTable.h>
#include <Wt/WLabel.h>
#include <Wt/WCssDecorationStyle.h>
#include <Wt/WLengthValidator.h>
#include <Wt/WEmailValidator.h>
#include <Wt/Auth/PasswordStrengthValidator.h>
#include <Wt/WMessageBox.h>

#define MAX_EDIT_LEN 20


AuthWidgets::AuthWidgets() {
   WApplication::instance()->useStyleSheet("/css/auth.css");
   WApplication::instance()->internalPathChanged().connect(this, &AuthWidgets::handleInternalPath);
   voffset_ = (char*)"20vh";
}

void AuthWidgets::setVOffset(char* voffset) { voffset_ = voffset; }

WTable *AuthWidgets::drawHead(std::string title, int &row) {
   clear();
   auto tbl = addWidget(std::make_unique<WTable>());
   tbl->elementAt(row, 0)->setColumnSpan(3);
   tbl->elementAt(row, 0)->setContentAlignment(AlignmentFlag::Top | AlignmentFlag::Center);
   tbl->elementAt(row, 0)->setPadding(10);
   WText *txt = tbl->elementAt(row,0)->addWidget(std::make_unique<WText>(title));
   txt->decorationStyle().font().setSize(FontSize::XLarge);
   row++;
   tbl->elementAt(row, 0)->setColumnSpan(3);
   message_ = tbl->elementAt(row, 0);
   message_->setPadding(3);
   message_->setHeight(16);
   WCssDecorationStyle& errorStyle = message_->decorationStyle();
   errorStyle.setForegroundColor(WColor("red"));
   errorStyle.font().setSize(FontSize::Smaller);
   errorStyle.font().setWeight(FontWeight::Bold);
   errorStyle.font().setStyle(FontStyle::Italic);
   row++;
   return tbl;
}

WLineEdit *AuthWidgets::drawInputFiled(WTable *tbl, std::string text, int &row, std::string name, WLineEdit *prev) {
   auto edit = tbl->elementAt(row,2)->addWidget(std::make_unique<WLineEdit>());
   edit->setStyleClass("auth-edit"); //setWidth(WLength(95, LengthUnit::Percentage));
   if(name == "email") {
     edit->setValidator(std::make_shared<WEmailValidator>());
   } else {
     edit->setValidator(std::make_shared<WLengthValidator>(2,MAX_EDIT_LEN));
   }
   edit->validator()->setMandatory(true);
   edit->setObjectName(name);
   auto label = tbl->elementAt(row,0)->addWidget(std::make_unique<WLabel>(text));
   label->setBuddy(edit);
   if(prev) prev->enterPressed().connect(std::bind(&AuthWidgets::setFocus, this, edit));
   row++;
   return edit;
}
void AuthWidgets::drawSubmitButton(WTable *tbl, int &row, std::string text) {
   WPushButton *submit = tbl->elementAt(row,0)->addWidget(std::make_unique<WPushButton>(text));
   submit->clicked().connect(this, &AuthWidgets::submit);
   submit->setStyleClass("login-butt");
}
void AuthWidgets::drawCancelButton(WTable *tbl, int &row) {
   WPushButton *cancel = tbl->elementAt(row,2)->addWidget(std::make_unique<WPushButton>("Отмена"));
   cancel->setLink(WLink(LinkType::InternalPath, "/auth"));
   cancel->setStyleClass("reg-butt");
}

void AuthWidgets::setFocus(WLineEdit *target) {
  target->setFocus();
}

void AuthWidgets::showRegister() {
   int row = 0;
   WTable *tbl = drawHead("Регистрация", row);
   login_ = drawInputFiled(tbl, "Логин:", row, "login", NULL);
   passw_ = drawInputFiled(tbl, "Пароль:", row, "password", login_);
   passw_->setEchoMode(EchoMode::Password);
   repeat_ = drawInputFiled(tbl, "Подтверждение:", row, "", passw_);
   repeat_->setEchoMode(EchoMode::Password);
   email_ = drawInputFiled(tbl, "Email:", row, "email", repeat_);
   email_->enterPressed().connect(this, &AuthWidgets::submit);
   drawSubmitButton(tbl, row, "Регистрация");
   drawCancelButton(tbl, row);
}

void AuthWidgets::showChangePass(std::string login) {
   int row = 0;
   WApplication::instance()->setInternalPath("/changepass", false);
   WTable *tbl = drawHead("Изменение пароля для " + login, row);
   passw_ = drawInputFiled(tbl, "Пароль:", row, "password", login_);
   passw_->setEchoMode(EchoMode::Password);
   repeat_ = drawInputFiled(tbl, "Подтверждение:", row, login, passw_);  //указываем login в качестве имени поля, чтобы передать параметр далее в submit
   repeat_->setEchoMode(EchoMode::Password);
   drawSubmitButton(tbl, row, "Изменить");
   drawCancelButton(tbl, row);
}

void AuthWidgets::showRestore() {
   int row = 0;
   WTable *tbl = drawHead("Восстановление доступа", row);
   email_ = drawInputFiled(tbl, "Email:", row, "email", NULL); //надо WEmailEdit
   email_->enterPressed().connect(this, &AuthWidgets::submit);
   drawSubmitButton(tbl, row, "Отправить");
   drawCancelButton(tbl, row);
}

void AuthWidgets::showAuth() {
   int row = 0;
   WApplication::instance()->setInternalPath("/auth", false);
   WTable *tbl = drawHead("Авторизация", row);
   login_ = drawInputFiled(tbl, "Логин:", row, "login", NULL);
   login_->enterPressed().connect(this, &AuthWidgets::submit);
   passw_ = drawInputFiled(tbl, "Пароль:", row, "password", login_);
   passw_->setEchoMode(EchoMode::Password);
   passw_->enterPressed().connect(this, &AuthWidgets::submit);
   drawSubmitButton(tbl, row, "Войти");
   WPushButton *reg = tbl->elementAt(row,2)->addWidget(std::make_unique<WPushButton>("Регистрация"));
   reg->setLink(WLink(LinkType::InternalPath, "/reg"));
   reg->setStyleClass("reg-butt");
   auto anchor = tbl->elementAt(row,2)->addWidget(std::make_unique<WAnchor>("/reg", "Восстановить"));
   anchor->setLink(WLink(LinkType::InternalPath, "/restore"));
   anchor->setStyleClass("reg-butt"); //надо получитьт правую центровку и оставить как есть.
}

void AuthWidgets::handleInternalPath(const std::string &internalPath)
{
    if (internalPath == "/auth")
      showAuth();
    else if (internalPath == "/reg")
      showRegister();
    else if (internalPath == "/restore")
      showRestore();
    else if (internalPath == "/changepass") {}
    else {
      WApplication::instance()->setInternalPath("/auth",  true);
      //showAuth();
    }
}

void AuthWidgets::putMessage(WLineEdit *edit, const WString& text) {
    message_->clear();
    message_->addWidget(std::make_unique<WText>(text));
    edit->label()->decorationStyle().setForegroundColor(WColor("red"));
}

void AuthWidgets::clearMessage(WLineEdit *edit) {
    message_->clear();
    edit->label()->decorationStyle().setForegroundColor(WColor());
}

bool AuthWidgets::checkValid(WLineEdit *edit, const WString& text) {
  if (edit->validate() != ValidationState::Valid) {
    putMessage(edit, text);
    return false;
  } else {
    clearMessage(edit);
    return true;
  }
}

std::unique_ptr<WMessageBox> messageBox_;

void AuthWidgets::messageBoxDone(StandardButton result) {
  messageBox_.reset();
}

void AuthWidgets::messageBox(std::string text) {
    messageBox_ = std::make_unique<WMessageBox>("Внимание!", text, Wt::Icon::None, Wt::StandardButton::None);
    auto Ok_ = messageBox_->addButton("Ok", Wt::StandardButton::Ok);
    messageBox_->setDefaultButton(Ok_);
    messageBox_->buttonClicked().connect(this, &AuthWidgets::messageBoxDone);
    messageBox_->setStyleClass("messagebox");
    messageBox_->setIcon(Wt::Icon::Warning);
//    messageBox_->setOffsets(0, Wt::Side::CenterX);
    messageBox_->setOffsets(WLength(voffset_), Side::Top);
    messageBox_->animateShow(Wt::WAnimation(Wt::AnimationEffect::Pop | Wt::AnimationEffect::Fade, Wt::TimingFunction::Linear, 100));
}

bool AuthWidgets::checkPasswords(std::string login, std::string email) {
    if(!checkValid(passw_, "Неверная длина пароля")) { return false; }
    auto pv = Auth::PasswordStrengthValidator();
    pv.setMinimumLength(Auth::PasswordStrengthType::FourCharClass, 7);
    auto result = pv.evaluateStrength(passw_->valueText().toUTF8(), login, email);
    if(!result.isValid()) {
      putMessage(passw_, "Пароль слишком слабый: " + std::to_string(result.strength()));
      return false;
    }
    if(passw_->valueText().toUTF8() != repeat_->valueText().toUTF8()) {
      putMessage(passw_, "Пароли не совпадают");
      return false;
    }
    return true;
}
void AuthWidgets::submit() {
  if(WApplication::instance()->internalPath() == "/auth") {
    if(!checkValid(login_, "Неверная длина логина")) { return; }
    std::string msg = "Логин не найден";
    if(findLogin(login_->valueText().toUTF8())) {
      if(checkLogin(login_->valueText().toUTF8(), passw_->valueText().toUTF8(), msg)) {
        loginDone_.emit(login_->valueText().toUTF8());
        return;
      }
    }
    putMessage(login_, msg);
    return;
  }
  if(WApplication::instance()->internalPath() == "/reg") {
    if(findLogin(login_->valueText().toUTF8())) {
      putMessage(login_, "Логин уже существует");
      return;
    }
    if(!checkValid(login_, "Неверная длина логина")) { return; }
    if(!checkPasswords(login_->valueText().toUTF8(), email_->valueText().toUTF8())) { return; }
    if(!checkValid(email_, "Неверный формат email")) { return; }
    clearMessage(passw_);
    if(!saveLogin(login_->valueText().toUTF8(), passw_->valueText().toUTF8(), email_->valueText().toUTF8())) {
      messageBox("Ошибка. Не удалось сохранить новый логин!");
      return;
    }
    WApplication::instance()->setInternalPath("/auth", true);  //переход на страницу авторизации
  }
  if(WApplication::instance()->internalPath() == "/changepass") {
    if(!checkPasswords(repeat_->objectName(), "somefake@email.com")) { return; }
    if(!changePassword(repeat_->objectName(), passw_->valueText().toUTF8())) {
      messageBox("Ошибка. Не удалось обновить пароль!");
      return;
    }
    WApplication::instance()->setInternalPath("/auth", true);  //переход на страницу авторизации
  }
  if(WApplication::instance()->internalPath() == "/restore") {
    if(!checkValid(email_, "Неверный формат email")) { return; }
    if(restoreLogin(email_->valueText().toUTF8())) {
      messageBox("Информация для сброса пароля направлена на " + email_->valueText().toUTF8());
      WApplication::instance()->setInternalPath("/auth", true);  //переход на страницу авторизации
    } else {
      messageBox("Не удалось отправить письмо на " + email_->valueText().toUTF8());
    }
    return;
  }
}
