#ifndef CREATEVENVFORM_H
#define CREATEVENVFORM_H

#include <QDialog>

namespace Ui
{
class CreateVenvForm;
}

class CreateVenvForm : public QDialog
{
    Q_OBJECT

  public:
    explicit CreateVenvForm(QWidget *parent = nullptr);
    ~CreateVenvForm();

    QString path() const;

  private:
    void promptForLocation();
    Ui::CreateVenvForm *ui;
};

#endif // CREATEVENVFORM_H
