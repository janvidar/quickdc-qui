/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_SETTINGSVIEW_H
#define HAVE_QUICKDC_QUI_SETTINGSVIEW_H

#include "view.h"

/*
 * For ReachabilityVerdict by value: this page keeps the last one seen for each
 * family, since one run answers for one family and the page shows both at once.
 */
#include <core/dropbridge.h>

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
class ReachabilityPanel;
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
 *
 * The reachability rows on the Connection tab are the one thing here that is not a
 * setting: they are what a measurement concluded, shown read-only beside the field
 * that turns the measurement on. That is inside the rule above rather than an
 * exception to it - the setting is the server's name, and an answer with nowhere to
 * be read is an answer nobody can act on.
 */
class SettingsView final : public View {
	Q_OBJECT

public:
	/**
	 * The page is built from whatever has been measured already, and asks for
	 * nothing and waits for nothing: a check is several round trips to a stranger's
	 * server that may not answer at all, so the rows are drawn with what is in hand
	 * and drawn again when there is more. The start-up check is usually finished long
	 * before this window is built, and asking for that answer again would be asking a
	 * third party for something already known.
	 *
	 * @param config where everything is read and written. May be null, in which
	 *        case the form is shown disabled.
	 * @param shares the shared folder list; may be null.
	 * @param drop where a reachability measurement comes from; may be null, in
	 *        which case the status rows say nothing has been measured and the
	 *        button that would ask for one is disabled.
	 */
	SettingsView(ConfigBridge* config, Shares* shares, DropBridge* drop = nullptr,
	             QWidget* parent = nullptr);

	QString title() const override;
	Glyph glyph() const override;

	/**
	 * Discard any edits and read the stored values again.
	 *
	 * The reachability server's name is read back with the rest and the status rows are
	 * left alone: a Revert undoes edits, and a measurement is not an edit, so throwing
	 * away what was measured because somebody pressed Revert would lose the one thing
	 * on this page that cannot be read again from the file.
	 *
	 * The local hub's fields are enabled from the switch here rather than left to its
	 * toggled signal, which does not fire for a value that has not changed - and the
	 * value has not changed on the load that follows a save. Its figures land in spin
	 * boxes whose ranges are the core's own clamps, so a figure edited into the file by
	 * hand is shown as what the hub will do with it rather than as what was typed.
	 */
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
	QWidget* buildLocalHub();
	QWidget* buildTransfers();
	QWidget* buildSharing();
	QWidget* buildChat();
	QWidget* buildBoards();
	QWidget* buildAppearance();

	/**
	 * A tab page: the form given its margins, and scrolled when the window is too
	 * short for it. Every tab goes through this, so that they are all inset by the
	 * same amount and none of them clips instead of scrolling.
	 */
	QWidget* makePage(QWidget* page);

	/**
	 * Show what the chosen connection mode is about, and hide the rest.
	 *
	 * Every mode keeps the reachability rows, since what the network does is worth
	 * knowing whichever mode asked; the plumbing, the forwarding and the forced claim
	 * belong to advanced alone, because in the other two modes they are derived rather
	 * than answered - see save(), which writes them from the mode and not from the
	 * controls.
	 *
	 * Called from the combo and again at the end of load(), for the reason the local
	 * hub's fields are: a signal does not fire for a value that has not changed, and on
	 * the load that follows a save the value has not changed.
	 */
	void applyConnectionMode();

	/** The note naming who a check asks, from what the core says is in force. */
	void describeAutomatic();

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

	/**
	 * Pass a verdict to the panel that draws it.
	 *
	 * A slot rather than a direct connection to the panel, because this page is what
	 * holds the bridge and the panel deliberately knows nothing about one.
	 */
	void showVerdict(const ReachabilityVerdict& verdict);

	/** Pass on why nobody measured, which says nothing about being reachable. */
	void showCheckFailed(const QString& reason);

	/**
	 * What the status rows say before anything has been measured.
	 *
	 * The paragraphs are hidden rather than left empty: three paragraphs of nothing
	 * between the button and the note below it is a gap somebody reads as a page still
	 * loading.
	 */
	void clearVerdict();

	/** A note under a group, in the muted colour. */
	QLabel* makeNote(const QString& text);

	ConfigBridge* m_config;
	Shares* m_shares;
	DropBridge* m_drop;

	QTabWidget* m_tabs;

	QLineEdit* m_nickname;
	QLineEdit* m_description;
	QLineEdit* m_email;
	QLabel* m_clientId;

	/*
	 * The mode, and the fields each mode is about, held one container to a mode.
	 *
	 * Containers rather than loose fields because a form layout hides a field without
	 * hiding the label beside it, and a page of orphaned labels is worse than a page
	 * that shows a setting the mode does not use. Advanced has two, since the claim it
	 * offers belongs under the measurement it overrides and its plumbing above it.
	 */
	QComboBox* m_mode;
	QWidget* m_automaticFields;
	QWidget* m_advancedFields;
	QWidget* m_advancedClaim;
	QWidget* m_passiveFields;
	QSpinBox* m_port;
	QLineEdit* m_bind;
	QLineEdit* m_address;

	/*
	 * Whether anything outside can reach the port above.
	 *
	 * Read-only, because a measurement is not a preference: a field offering to edit
	 * one would be offering to edit the network. The panel holds the verdicts and
	 * everything said about them, one per family, and this page does no more than
	 * pass them on. That is what lets a test put an answer on screen - every settings
	 * test builds this page with no bridge behind it, so nothing here would otherwise
	 * ever be drawn with a verdict in it.
	 */
	ReachabilityPanel* m_reachability;

	/** What automatic does on the user's behalf, naming the server the core asks. */
	QLabel* m_automaticNote = nullptr;

	/*
	 * Announce active mode whatever was measured, which the core reads before it looks
	 * at the measurement at all.
	 *
	 * Under the switch it overrides rather than beside it, because the two can disagree
	 * on screen and this one wins: a page that put the claim above the answer it defeats
	 * would be showing the answer as though it still applied. It is a setting and not a
	 * verdict, so it is the one editable thing below the read-only rows.
	 *
	 * What it costs is said on the page rather than in the tooltip, because it is the
	 * one setting here whose cost is paid by somebody else: a peer acts on the claim by
	 * connecting, and when it cannot the transfer fails at that peer's end, so the
	 * symptom never appears beside the switch that caused it.
	 */
	QCheckBox* m_forceActive;

	/*
	 * Ask the internet gateway to forward the peer port, off unless somebody turns it
	 * on.
	 *
	 * A setting rather than something the client does when a check says passive: a
	 * search tells every host on the local segment that this machine is here and what
	 * it runs, and that disclosure is the cost of the feature. It sits with the port it
	 * is about, since what it forwards is that port and no other.
	 */
	QCheckBox* m_useUpnp;

	QSpinBox* m_slots;
	QSpinBox* m_maxDownloads;
	QSpinBox* m_chunkSize;

	/*
	 * The hub this program can run itself, off unless somebody turns it on.
	 *
	 * Everything below the switch is what the core's own local hub reads, and every
	 * bound on them is the bound it clamps to - a figure it would quietly pull into
	 * range is one this page should not have offered. The fields are held together by
	 * 'm_localHubFields', which is enabled and disabled with the switch so that the
	 * labels grey out with the values rather than standing over dead controls.
	 *
	 * 'm_localHubSendQueue' is shown in kibibytes and stored in bytes, the way the
	 * chunk size on Transfers is.
	 */
	QCheckBox* m_localHub;
	QWidget* m_localHubFields;
	QLineEdit* m_localHubName;
	QLineEdit* m_localHubDescription;
	QSpinBox* m_localHubUsers;
	QSpinBox* m_localHubHistory;
	QSpinBox* m_localHubPerAddress;
	QSpinBox* m_localHubHandshakes;
	QSpinBox* m_localHubHandshakeTimeout;
	QSpinBox* m_localHubIdleTimeout;
	QSpinBox* m_localHubLinger;
	QSpinBox* m_localHubSendQueue;
	QCheckBox* m_localHubCheckClaims;

	QLineEdit* m_downloadDir;

	/** The scheme, by name, with the name itself as the item's data. */
	QComboBox* m_theme;

	/* Pictures in chat: whether they arrive by themselves, and how big they may be. */
	QCheckBox* m_fetchMedia;
	QSpinBox* m_mediaLimit;
	QSpinBox* m_mediaKeep;

	/*
	 * Bulletin boards: the two disclosures, and what this client is willing to hold.
	 * Reading a post is fetching a file from whoever has it, so both of the first two
	 * say something about this user to somebody else - which is why the page says so
	 * in words rather than leaving it to a tooltip.
	 */
	QCheckBox* m_prefetchPosts;
	QCheckBox* m_servePosts;
	QSpinBox* m_postKeep;
	QSpinBox* m_postCache;
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
