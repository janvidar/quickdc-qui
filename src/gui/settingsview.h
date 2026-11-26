/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_SETTINGSVIEW_H
#define HAVE_QUICKDC_QUI_SETTINGSVIEW_H

#include "view.h"

#include <core/dropbridge.h>
#include <core/uisettings.h>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace Qdc {

class ConfigBridge;
class ReachabilityPanel;
class Shares;

/**
 * The configuration, a page per subject, applied as it is changed: a text field when
 * it is left, anything else at once. Each field is bound to its setting once, and
 * follows the setting when it changes from elsewhere unless it is being edited.
 */
class SettingsView final : public View {
	Q_OBJECT

public:
	SettingsView(ConfigBridge* config, Shares* shares, DropBridge* drop = nullptr,
	             QWidget* parent = nullptr);

	QString title() const override;
	Glyph glyph() const override;

	/** Every field from the configuration as it stands. */
	void load();

private:
	QWidget* buildIdentity();
	QWidget* buildConnection();
	QWidget* buildLocalHub();
	QWidget* buildTransfers();
	QWidget* buildSharing();
	QWidget* buildChat();
	QWidget* buildBoards();
	QWidget* buildAppearance();

	void addPage(const QString& name, QWidget* content);

	/** A collapsed "More settings" disclosure over 'content', placed where it is added. */
	QWidget* advancedSection(QWidget* content);

	QLabel* makeHint(const QString& text, QWidget* parent);

	void bind(QLineEdit* field, const Settings::Text& setting);
	void bind(QSpinBox* field, const Settings::Number& setting, int scale = 1);
	void bind(QCheckBox* field, const Settings::Flag& setting);

	void applyConnectionMode();
	void submitConnectionMode();
	void showPeerPort();

	void refreshShareList();
	void chooseDirectory(const QString& title, const QString& start,
	                     std::function<void(const QString&)> chosen);
	void addShare();
	void removeSelectedShare();
	void renameSelectedShare();
	void chooseDownloadDirectory();

	ConfigBridge* m_config;
	Shares* m_shares;
	DropBridge* m_drop;

	QListWidget* m_pages;
	QStackedWidget* m_stack;
	QLabel* m_status;

	/** Each puts one field back to what its setting says. */
	std::vector<std::function<void()>> m_loaders;

	QLabel* m_clientId = nullptr;

	QComboBox* m_mode = nullptr;
	QFormLayout* m_connectionForm = nullptr;
	QLayout* m_portRow = nullptr;
	QWidget* m_advancedMore = nullptr;
	QLabel* m_modeHint = nullptr;
	QSpinBox* m_port = nullptr;
	QLabel* m_portStatus = nullptr;
	QLabel* m_automaticPort = nullptr;
	QCheckBox* m_useUpnp = nullptr;
	QCheckBox* m_forceActive = nullptr;
	ReachabilityPanel* m_reachability = nullptr;

	QCheckBox* m_localHub = nullptr;
	QWidget* m_localHubFields = nullptr;
	QLabel* m_localHubHint = nullptr;

	QLineEdit* m_downloadDir = nullptr;
	QComboBox* m_theme = nullptr;

	QListWidget* m_shareList = nullptr;
	QPushButton* m_removeShare = nullptr;
	QPushButton* m_renameShare = nullptr;

	bool m_loading = false;
};

}

#endif // HAVE_QUICKDC_QUI_SETTINGSVIEW_H
