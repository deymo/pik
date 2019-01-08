#include "codec_comparison_window.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <functional>
#include <utility>

#include <QCollator>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFlags>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QX11Info>
#include <QtConcurrent>

#include "../../x11/icc.h"
#include "image_loading.h"
#include "split_image_view.h"

namespace pik {

static constexpr char kPngSuffix[] = "png";

namespace {

QVector<QPair<QComboBox*, QString>> currentCodecSelection(
    const Ui::CodecComparisonWindow& ui) {
  QVector<QPair<QComboBox*, QString>> result;
  for (QComboBox* const comboBox :
       {ui.codec1ComboBox, ui.codec2ComboBox, ui.compressionLevel1ComboBox,
        ui.compressionLevel2ComboBox}) {
    result << qMakePair(comboBox, comboBox->currentText());
  }
  return result;
}

void restoreCodecSelection(
    const QVector<QPair<QComboBox*, QString>>& selection) {
  for (const auto& comboBox : selection) {
    const int index = comboBox.first->findText(comboBox.second);
    if (index != -1) {
      comboBox.first->setCurrentIndex(index);
    }
  }
}

}  // namespace

CodecComparisonWindow::CodecComparisonWindow(QWidget* const parent)
    : QMainWindow(parent),
      monitorIccProfile_(pik::GetMonitorIccProfile(QX11Info::connection(),
                                                   QX11Info::appScreen())) {
  ui_.setupUi(this);

  connect(ui_.imageSetComboBox, &QComboBox::currentTextChanged, this,
          &CodecComparisonWindow::handleImageSetSelection);
  connect(ui_.imageComboBox, &QComboBox::currentTextChanged, this,
          &CodecComparisonWindow::handleImageSelection);

  connect(ui_.codec1ComboBox, &QComboBox::currentTextChanged,
          [this]() { handleCodecChange(Side::LEFT); });
  connect(ui_.codec2ComboBox, &QComboBox::currentTextChanged,
          [this]() { handleCodecChange(Side::RIGHT); });

  connect(ui_.compressionLevel1ComboBox, &QComboBox::currentTextChanged,
          [this]() { updateSideImage(Side::LEFT); });
  connect(ui_.compressionLevel2ComboBox, &QComboBox::currentTextChanged,
          [this]() { updateSideImage(Side::RIGHT); });

  connect(ui_.match1Label, &QLabel::linkActivated,
          [this]() { matchSize(Side::LEFT); });
  connect(ui_.match2Label, &QLabel::linkActivated,
          [this]() { matchSize(Side::RIGHT); });

  connect(
      ui_.splitImageView, &SplitImageView::renderingModeChanged,
      [this](const SplitImageRenderingMode newMode) {
        switch (newMode) {
          case SplitImageRenderingMode::LEFT:
          case SplitImageRenderingMode::RIGHT: {
            QString codec, compressionLevel;
            if (newMode == SplitImageRenderingMode::LEFT) {
              codec = ui_.codec1ComboBox->currentText();
              compressionLevel = ui_.compressionLevel1ComboBox->currentText();
            } else {
              codec = ui_.codec2ComboBox->currentText();
              compressionLevel = ui_.compressionLevel2ComboBox->currentText();
            }
            ui_.renderingModeLabel->setText(tr("Currently displaying: %1 @ %2")
                                                .arg(codec)
                                                .arg(compressionLevel));
            break;
          }

          case SplitImageRenderingMode::MIDDLE:
            ui_.renderingModeLabel->setText(
                tr("Currently displaying the original image."));
            break;

          default:
            ui_.renderingModeLabel->clear();
            break;
        }
      });
}

CodecComparisonWindow::CodecComparisonWindow(const QString& directory,
                                             QWidget* const parent)
    : CodecComparisonWindow(parent) {
  loadDirectory(directory);
}

void CodecComparisonWindow::handleImageSetSelection(
    const QString& imageSetName) {
  const auto selection = currentCodecSelection(ui_);
  {
    const QSignalBlocker blocker(ui_.imageComboBox);
    ui_.imageComboBox->clear();
  }
  const QStringList imageNames = imageSets_.value(imageSetName).keys();
  const std::function<QIcon(const QString&)> loadIcon =
      [this, &imageSetName](const QString& imageName) {
        return QIcon(pathToOriginalImage(imageSetName, imageName));
      };
  const QFuture<QIcon> thumbnails = QtConcurrent::mapped(imageNames, loadIcon);
  int i = 0;
  for (const QString& imageName : imageNames) {
    ui_.imageComboBox->addItem(thumbnails.resultAt(i), imageName);
    ++i;
  }
  restoreCodecSelection(selection);
}

void CodecComparisonWindow::handleImageSelection(const QString& imageName) {
  const QString imageSetName = ui_.imageSetComboBox->currentText();
  ui_.splitImageView->setMiddleImage(loadImage(
      pathToOriginalImage(imageSetName, imageName), monitorIccProfile_));

  const auto selection = currentCodecSelection(ui_);
  QStringList codecs = imageSets_.value(imageSetName).value(imageName).keys();
  for (QComboBox* const codecComboBox :
       {ui_.codec1ComboBox, ui_.codec2ComboBox}) {
    {
      const QSignalBlocker blocker(codecComboBox);
      codecComboBox->clear();
    }
    codecComboBox->addItems(codecs);
  }
  restoreCodecSelection(selection);
}

void CodecComparisonWindow::handleCodecChange(const Side side) {
  const QComboBox* const codecComboBox =
      side == Side::LEFT ? ui_.codec1ComboBox : ui_.codec2ComboBox;
  QComboBox* const compressionLevelComboBox =
      side == Side::LEFT ? ui_.compressionLevel1ComboBox
                         : ui_.compressionLevel2ComboBox;

  QStringList compressionLevels =
      imageSets_.value(ui_.imageSetComboBox->currentText())
          .value(ui_.imageComboBox->currentText())
          .value(codecComboBox->currentText())
          .keys();
  QCollator collator;
  collator.setNumericMode(true);
  std::sort(compressionLevels.begin(), compressionLevels.end(), collator);

  {
    const QSignalBlocker blocker(compressionLevelComboBox);
    compressionLevelComboBox->clear();
  }
  compressionLevelComboBox->addItems(compressionLevels);
  matchSize(side);
}

void CodecComparisonWindow::updateSideImage(const Side side) {
  const ComparableImage& imageInfo = currentlySelectedImage(side);
  if (imageInfo.decodedImagePath.isEmpty()) return;
  QImage image = loadImage(imageInfo.decodedImagePath, monitorIccProfile_);
  const int pixels = image.width() * image.height();
  QLabel* const sizeInfoLabel =
      side == Side::LEFT ? ui_.size1Label : ui_.size2Label;
  if (pixels == 0) {
    sizeInfoLabel->setText(tr("Empty image."));
  } else {
    const double bpp =
        CHAR_BIT * static_cast<double>(imageInfo.byteSize) / pixels;
    sizeInfoLabel->setText(tr("%L1bpp").arg(bpp));
  }

  if (side == Side::LEFT) {
    ui_.splitImageView->setLeftImage(std::move(image));
  } else {
    ui_.splitImageView->setRightImage(std::move(image));
  }
}

QString CodecComparisonWindow::pathToOriginalImage(
    const QString& imageSetName, const QString& imageName) const {
  return baseDirectory_.absolutePath() + "/" + imageSetName + "/" + imageName +
         "/original.png";
}

CodecComparisonWindow::ComparableImage
CodecComparisonWindow::currentlySelectedImage(const Side side) const {
  const QComboBox* const codecComboBox =
      side == Side::LEFT ? ui_.codec1ComboBox : ui_.codec2ComboBox;
  QComboBox* const compressionLevelComboBox =
      side == Side::LEFT ? ui_.compressionLevel1ComboBox
                         : ui_.compressionLevel2ComboBox;

  return imageSets_.value(ui_.imageSetComboBox->currentText())
      .value(ui_.imageComboBox->currentText())
      .value(codecComboBox->currentText())
      .value(compressionLevelComboBox->currentText());
}

void CodecComparisonWindow::matchSize(const Side side) {
  const Side otherSide = (side == Side::LEFT ? Side::RIGHT : Side::LEFT);
  const qint64 otherSideSize = currentlySelectedImage(otherSide).byteSize;
  if (otherSideSize == 0) return;

  const QComboBox* const codecComboBox =
      side == Side::LEFT ? ui_.codec1ComboBox : ui_.codec2ComboBox;
  QComboBox* const compressionLevelComboBox =
      side == Side::LEFT ? ui_.compressionLevel1ComboBox
                         : ui_.compressionLevel2ComboBox;
  const Codec codec = imageSets_.value(ui_.imageSetComboBox->currentText())
                          .value(ui_.imageComboBox->currentText())
                          .value(codecComboBox->currentText());
  if (codec.empty()) return;
  Codec::ConstIterator bestMatch = codec.begin();
  for (auto it = codec.begin(); it != codec.end(); ++it) {
    if (std::abs(it->byteSize - otherSideSize) <
        std::abs(bestMatch->byteSize - otherSideSize)) {
      bestMatch = it;
    }
  }
  compressionLevelComboBox->setCurrentText(bestMatch.key());
}

void CodecComparisonWindow::loadDirectory(const QString& directory) {
  baseDirectory_ = directory;
  baseDirectory_.makeAbsolute();
  imageSets_.clear();
  visited_.clear();

  browseDirectory(directory);

  {
    const QSignalBlocker blocker(ui_.imageSetComboBox);
    ui_.imageSetComboBox->clear();
  }
  ui_.imageSetComboBox->addItems(imageSets_.keys());
}

void CodecComparisonWindow::browseDirectory(const QDir& directory, int depth) {
  for (const QFileInfo& subdirectory : directory.entryInfoList(
           QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
    if (visited_.contains(subdirectory.absoluteFilePath())) continue;
    visited_.insert(subdirectory.absoluteFilePath());
    browseDirectory(subdirectory.absoluteFilePath(), depth + 1);
  }

  // Need at least image_name/codec_name/file.
  if (depth < 2) return;

  for (const QFileInfo& file :
       directory.entryInfoList(QDir::Files | QDir::NoSymLinks)) {
    if (file.suffix() == kPngSuffix) continue;
    QString decodedImage;
    if (QImageReader::supportedImageFormats().contains(
            file.suffix().toLower().toLatin1())) {
      decodedImage = file.absoluteFilePath();
    } else {
      QFileInfo png(file.absolutePath() + "/" + file.completeBaseName() + "." +
                    kPngSuffix);
      if (png.exists()) {
        decodedImage = png.absoluteFilePath();
      }
    }

    if (decodedImage.isEmpty()) continue;

    const QString codec = file.absoluteDir().dirName();
    QDir imageDirectory = file.absoluteDir();
    if (!imageDirectory.cdUp()) return;
    const QString imageName = imageDirectory.dirName();
    QDir imageSetDirectory = imageDirectory;
    if (!imageSetDirectory.cdUp()) return;
    QString imageSetPath =
        baseDirectory_.relativeFilePath(imageSetDirectory.absolutePath());
    if (imageSetPath.isEmpty()) {
      imageSetPath = ".";
    }

    ComparableImage& image =
        imageSets_[imageSetPath][imageName][codec][file.completeBaseName()];
    image.decodedImagePath = decodedImage;
    image.byteSize = file.size();
  }
}

}  // namespace pik