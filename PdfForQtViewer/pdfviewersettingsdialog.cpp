#include "pdfviewersettingsdialog.h"
#include "ui_pdfviewersettingsdialog.h"

#include "pdfglobal.h"
#include "pdfutils.h"

#include <QListWidgetItem>

namespace pdfviewer
{

PDFViewerSettingsDialog::PDFViewerSettingsDialog(const PDFViewerSettings::Settings& settings, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PDFViewerSettingsDialog),
    m_settings(settings),
    m_isLoadingData(false)
{
    ui->setupUi(this);

    new QListWidgetItem(QIcon(":/resources/engine.svg"), tr("Engine"), ui->optionsPagesWidget, EngineSettings);
    new QListWidgetItem(QIcon(":/resources/rendering.svg"), tr("Rendering"), ui->optionsPagesWidget, RenderingSettings);
    new QListWidgetItem(QIcon(":/resources/shading.svg"), tr("Shading"), ui->optionsPagesWidget, ShadingSettings);

    ui->renderingEngineComboBox->addItem(tr("Software"), static_cast<int>(pdf::RendererEngine::Software));
    ui->renderingEngineComboBox->addItem(tr("Hardware accelerated (OpenGL)"), static_cast<int>(pdf::RendererEngine::OpenGL));

    for (int i : { 1, 2, 4, 8, 16 })
    {
        ui->multisampleAntialiasingSamplesCountComboBox->addItem(QString::number(i), i);
    }

    for (QWidget* widget : { ui->engineInfoLabel, ui->renderingInfoLabel })
    {
        widget->setMinimumWidth(widget->sizeHint().width());
    }

    for (QCheckBox* checkBox : { ui->multisampleAntialiasingCheckBox, ui->antialiasingCheckBox, ui->textAntialiasingCheckBox, ui->smoothPicturesCheckBox, ui->ignoreOptionalContentCheckBox, ui->clipToCropBoxCheckBox })
    {
        connect(checkBox, &QCheckBox::clicked, this, &PDFViewerSettingsDialog::saveData);
    }
    for (QComboBox* comboBox : { ui->renderingEngineComboBox, ui->multisampleAntialiasingSamplesCountComboBox })
    {
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFViewerSettingsDialog::saveData);
    }

    ui->optionsPagesWidget->setCurrentRow(0);
    adjustSize();
    loadData();
}

PDFViewerSettingsDialog::~PDFViewerSettingsDialog()
{
    delete ui;
}

void PDFViewerSettingsDialog::on_optionsPagesWidget_currentItemChanged(QListWidgetItem* current, QListWidgetItem* previous)
{
    Q_UNUSED(previous);

    switch (current->type())
    {
        case EngineSettings:
            ui->stackedWidget->setCurrentWidget(ui->enginePage);
            break;

        case RenderingSettings:
            ui->stackedWidget->setCurrentWidget(ui->renderingPage);
            break;

        case ShadingSettings:
            ui->stackedWidget->setCurrentWidget(ui->shadingPage);
            break;

        default:
            Q_ASSERT(false);
            break;
    }
}

void PDFViewerSettingsDialog::loadData()
{
    pdf::PDFTemporaryValueChange guard(&m_isLoadingData, true);
    ui->renderingEngineComboBox->setCurrentIndex(ui->renderingEngineComboBox->findData(static_cast<int>(m_settings.m_rendererEngine)));

    if (m_settings.m_rendererEngine == pdf::RendererEngine::OpenGL)
    {
        ui->multisampleAntialiasingCheckBox->setEnabled(true);
        ui->multisampleAntialiasingCheckBox->setChecked(m_settings.m_multisampleAntialiasing);

        if (m_settings.m_multisampleAntialiasing)
        {
            ui->multisampleAntialiasingSamplesCountComboBox->setEnabled(true);
            ui->multisampleAntialiasingSamplesCountComboBox->setCurrentIndex(ui->multisampleAntialiasingSamplesCountComboBox->findData(m_settings.m_rendererSamples));
        }
        else
        {
            ui->multisampleAntialiasingSamplesCountComboBox->setEnabled(false);
            ui->multisampleAntialiasingSamplesCountComboBox->setCurrentIndex(-1);
        }
    }
    else
    {
        ui->multisampleAntialiasingCheckBox->setEnabled(false);
        ui->multisampleAntialiasingCheckBox->setChecked(false);
        ui->multisampleAntialiasingSamplesCountComboBox->setEnabled(false);
        ui->multisampleAntialiasingSamplesCountComboBox->setCurrentIndex(-1);
    }

    ui->antialiasingCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::Antialiasing));
    ui->textAntialiasingCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::TextAntialiasing));
    ui->smoothPicturesCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::SmoothImages));
    ui->ignoreOptionalContentCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::IgnoreOptionalContent));
    ui->clipToCropBoxCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::ClipToCropBox));
}

void PDFViewerSettingsDialog::saveData()
{
    if (m_isLoadingData)
    {
        return;
    }

    QObject* sender = this->sender();

    if (sender == ui->renderingEngineComboBox)
    {
        m_settings.m_rendererEngine = static_cast<pdf::RendererEngine>(ui->renderingEngineComboBox->currentData().toInt());
    }
    else if (sender == ui->multisampleAntialiasingCheckBox)
    {
        m_settings.m_multisampleAntialiasing = ui->multisampleAntialiasingCheckBox->isChecked();
    }
    else if (sender == ui->multisampleAntialiasingSamplesCountComboBox)
    {
        m_settings.m_rendererSamples = ui->multisampleAntialiasingSamplesCountComboBox->currentData().toInt();
    }
    else if (sender == ui->antialiasingCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::Antialiasing, ui->antialiasingCheckBox->isChecked());
    }
    else if (sender == ui->textAntialiasingCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::TextAntialiasing, ui->textAntialiasingCheckBox->isChecked());
    }
    else if (sender == ui->smoothPicturesCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::SmoothImages, ui->smoothPicturesCheckBox->isChecked());
    }
    else if (sender == ui->ignoreOptionalContentCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::IgnoreOptionalContent, ui->ignoreOptionalContentCheckBox->isChecked());
    }
    else if (sender == ui->clipToCropBoxCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::ClipToCropBox, ui->clipToCropBoxCheckBox->isChecked());
    }

    loadData();
}

}   // namespace pdfviewer
