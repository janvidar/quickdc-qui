/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_SETTINGSVIEW_H
#define HAVE_QUICKDC_QUI_SETTINGSVIEW_H

#include "view.h"

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace Qdc {

class ConfigBridge;
class Shares;

/**
 * The configuration, bound to the core's, a tab at a time.
 *
 * One tab per subject rather than one page of everything, so that a setting is
 * found by reading the tab strip rather than by scrolling. Save and Revert sit
 * below the tabs and belong to all of them, because a save writes every field
 * wherever it happens to be showing.
 *
 * Loaded on construction and written only when Save is pressed, rather than on
 * every keystroke: half a nickname is not a nickname, and the store writes the
 * whole file each time.
 *
 * Only settings the core actually reads are here. There is no control for
 * anything it would merely store and ignore - a switch that does nothing is worse
 * than an absent one. Two things the plan asks for are absent for that reason and
 * say so in the form itself: a global rate limit, which nothing in the core
 * enforces, and a separate folder for incomplete files, which the core does not
 * have - a partial download lives beside its finished name with ".part" appended.
 */
class SettingsView final : public View {
	Q_OBJECT

public:
	/**
	 * @param config where everything is read and written. May be null, in which
	 *        case the form is shown disabled.
	 * @param shares the shared folder list; may be null.
	 */
	SettingsView(ConfigBridge* config, Shares* shares, QWidget* parent = nullptr);

	QString title() const override;
	Glyph glyph() const override;

	/** Discard any edits and read the stored values again. */
	void load();

	/**
	 * Write every field, save the file, and tell the core.
	 * @return false if the file could not be written.
	 */
	bool save();

	/** Whether anything has been edited since the last load or save. */
	bool isModified() const;

signals:
	/** Saved successfully. The download directory may have changed. */
	void saved();

private:
	QWidget* buildIdentity();
	QWidget* buildConnection();
	QWidget* buildTransfers();
	QWidget* buildSharing();
	QWidget* buildChat();
	QWidget* buildAppearance();

	/**
	 * A tab page: the form given its margins, and scrolled when the window is too
	 * short for it. Every tab goes through this, so that the six of them are inset
	 * by the same amount and none of them clips instead of scrolling.
	 */
	QWidget* makePage(QWidget* page);

	/** Mark the form dirty and enable Save. Connected to every field. */
	void markModified();

	void refreshShareList();

	/**
	 * Ask for a folder, and do something with it once there is one.
	 *
	 * The chooser is opened a turn later, so 'chosen' is called after this returns
	 * and only when something was picked; see the note where it is built.
	 */
	void chooseDirectory(const QString& title, const QString& start,
	                     std::function<void(const QString&)> chosen);

	void addShare();
	void removeSelectedShare();
	void renameSelectedShare();
	void chooseDownloadDirectory();

	/** A note under a group, in the muted colour. */
	QLabel* makeNote(const QString& text);

	ConfigBridge* m_config;
	Shares* m_shares;

	QTabWidget* m_tabs;

	QLineEdit* m_nickname;
	QLineEdit* m_description;
	QLineEdit* m_email;
	QLabel* m_clientId;

	QComboBox* m_mode;
	QSpinBox* m_port;
	QLineEdit* m_bind;
	QLineEdit* m_address;

	QSpinBox* m_slots;
	QSpinBox* m_maxDownloads;
	QSpinBox* m_chunkSize;

	QLineEdit* m_downloadDir;

	/** The scheme, by name, with the name itself as the item's data. */
	QComboBox* m_theme;

	/* Pictures in chat: whether they arrive by themselves, and how big they may be. */
	QCheckBox* m_fetchMedia;
	QSpinBox* m_mediaLimit;
	QSpinBox* m_mediaKeep;
	QListWidget* m_shareList;
	QPushButton* m_removeShare;
	QPushButton* m_renameShare;

	QPushButton* m_save;
	QPushButton* m_revert;
	QLabel* m_status;

	/**
	 * True while load() is populating the fields, so that setting them does not
	 * count as the user editing them.
	 */
	bool m_loading = false;

	bool m_modified = false;
};

}

#endif // HAVE_QUICKDC_QUI_SETTINGSVIEW_H
