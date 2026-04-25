#ifndef AUTHWIDGETS_HPP_INCLUDED
#define AUTHWIDGETS_HPP_INCLUDED

#include <Wt/WContainerWidget.h>

using namespace Wt;


class AuthWidgets : public WContainerWidget
{
public:
  AuthWidgets();
  void messageBox(std::string text);
  virtual bool findLogin(std::string login) { return true; };
  virtual bool checkLogin(std::string login, std::string password, std::string& msg) { return true; };
  virtual bool saveLogin(std::string login, std::string password, std::string email) { return true; };
  virtual bool restoreLogin(std::string email) { return true; };
  virtual bool changePassword(std::string login, std::string password) { return true; };
  Signal<std::string>& loginDone() { return loginDone_; }
  void showAuth();
  void showChangePass(std::string login);
  void setVOffset(char* voffset);
  void submit();
private:
  void showRegister();
  void showRestore();
  WTable *drawHead(std::string title, int &row);
  void drawSubmitButton(WTable *tbl, int &row, std::string text);
  void drawCancelButton(WTable *tbl, int &row);
  WLineEdit *drawInputFiled(WTable *tbl, std::string text, int &row, std::string name, WLineEdit *prev);
  bool checkValid(WLineEdit *edit, const WString& text);
  bool checkPasswords(std::string login, std::string email);
  void handleInternalPath(const std::string &internalPath);
  void putMessage(WLineEdit *edit, const WString& text);
  void clearMessage(WLineEdit *edit);
  void messageBoxDone(StandardButton result);
  void setFocus(WLineEdit *target);
  WContainerWidget *message_;
  WLineEdit *login_;
  WLineEdit *passw_;
  WLineEdit *email_;
  WLineEdit *repeat_;
  Signal<std::string> loginDone_;
  char *voffset_;
};


#endif // AUTHWIDGETS_HPP_INCLUDED
