#include "CreateVenvForm.h"
#include "ui_CreateVenvForm.h"

#include <QFileDialog>

CreateVenvForm::CreateVenvForm(QWidget *parent) : QDialog(parent), ui(new Ui::CreateVenvForm)
{
    ui->setupUi(this);

    connect(ui->locationBtn, &QPushButton::clicked, this, &CreateVenvForm::promptForLocation);
}

CreateVenvForm::~CreateVenvForm()
{
    delete ui;
}

QString CreateVenvForm::path() const
{
    return QString("%1/%2").arg(ui->locationEdit->text(), ui->nameEdit->text());
}

void CreateVenvForm::promptForLocation()
{
    QString selectedDir = QFileDialog::getExistingDirectory(
        this, "Python Environment Root", ui->locationEdit->text());
    ui->locationEdit->setText(selectedDir);
}
