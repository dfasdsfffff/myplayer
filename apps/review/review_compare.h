#pragma once

#include <QImage>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <memory>

namespace player_sdk_qt {
class QtPlayer;
class VideoWidget;
}

namespace review {

//! A self-contained A/B comparison surface.
//!
//! The primary player is not bound through VideoWidget::setPlayer(): the review
//! window may already have its one-view binding.  Frames are subscribed to and
//! forwarded to an independent VideoWidget instead.  Synchronisation is clock
//! based, not frame locked: A's state and explicit seeks are mirrored to B, but
//! playback does not poll-seek B on every position notification.
class ReviewCompare final : public QWidget {
    Q_OBJECT
public:
    enum class Mode { SideBySide, Wipe, Overlay, Difference };
    Q_ENUM(Mode)

    explicit ReviewCompare(QWidget* parent = nullptr);
    ~ReviewCompare() override;

    void setPrimary(player_sdk_qt::QtPlayer* player);
    bool setSecondary(const QString& localPath);
    void clearSecondary();

    //! B time is mapped as B = A + offsetMs.
    void setOffset(qint64 offsetMs);
    qint64 offset() const;

    void setMode(Mode mode);
    Mode mode() const;

    //! Freeze B at its currently presented frame.  A keeps its clock.
    void captureReference();
    //! Resume normal B synchronisation after captureReference().
    void clearReference();

    //! The host should call this when A's playback rate changes.  QtPlayer has
    //! no playbackRateChanged signal, so this cannot be inferred from A.
    void setPlaybackRate(double rate);

    //! Optional controls for the Wipe and Overlay modes. Values are [0, 1].
    //! Wipe shows B from the left through wipePosition(); overlayAmount is B's
    //! display-domain opacity.
    void setWipePosition(double position);
    double wipePosition() const;
    void setOverlayAmount(double amount);
    double overlayAmount() const;

    // Read-only state useful to window integration and tests.
    player_sdk_qt::QtPlayer* primary() const;
    player_sdk_qt::QtPlayer* secondary() const;
    player_sdk_qt::VideoWidget* primaryView() const;
    player_sdk_qt::VideoWidget* secondaryView() const;
    QString secondaryPath() const;
    qint64 secondaryDurationMs() const;
    bool secondaryOutOfRange() const;
    bool hasReference() const;
    bool primaryPlaying() const;
    //! Always false: this component deliberately does not promise frame lock.
    bool isFrameLocked() const;

signals:
    void errorOccurred(QString message);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace review
