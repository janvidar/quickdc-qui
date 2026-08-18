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
	 * Show what a measurement concluded, whatever it concluded.
	 *
	 * A run that established nothing is not an answer, whatever else it carries, and is
	 * shown as nothing having been measured.
	 *
	 * Which family was measured is part of the answer rather than a detail of how it was
	 * arrived at: one connection has one source address, so a run answers for IPv4 or
	 * for IPv6 and never for both, and a dual-stack host reachable over one of them
	 * would otherwise read as reachable full stop. The classification beside the address
	 * is the server's own word for what it saw, and that address need not be one this
	 * machine has - behind a carrier-grade NAT it is the provider's - which is exactly
	 * what the classification is there to say.
	 *
	 * The per-port outcomes are shown in the words they arrived in and only for the
	 * ports that were tested: an empty one is a test the server does not offer rather
	 * than a port that failed, and reporting it as a failure would argue somebody into
	 * turning off a feature that works. The reason and the remedies go up as they were
	 * written, one remedy to a line, bulleted and not a word otherwise - whoever
	 * measured wrote them for a user to read, and a client that reworded them would hand
	 * somebody comparing two clients two accounts of one network and no way to tell
	 * which to believe.
	 *
	 * A time that is not valid is a measurement the core recorded no time for, and is
	 * shown as never rather than as the epoch: dating an answer to 1970 reads as a bug
	 * in the clock instead of as a gap in what was recorded.
	 */
	void showVerdict(const ReachabilityVerdict& verdict);

	/**
	 * Show why nobody measured, which says nothing about being reachable.
	 *
	 * Only that one line changes and every row above it is left exactly as it was. A
	 * check that could not be run says nothing about whether this client is reachable -
	 * only that nobody found out this time - so an earlier measurement is still the best
	 * thing known, and the time it was taken is already beside it.
	 */
	void showCheckFailed(const QString& reason);

	/**
	 * What the status rows say before anything has been measured.
	 *
	 * The paragraphs are hidden rather than left empty: three paragraphs of nothing
	 * between the button and the note below it is a gap somebody reads as a page still
	 * loading.
	 */
	void clearVerdict();

	/**
	 * The last verdict seen for each family, since one run answers for one.
	 *
	 * Kept here rather than drawn as they arrive, because the page shows both at
	 * once and they arrive one after the other: rendering each on its own would
	 * have the second wipe the first off a page claiming to describe the client.
	 */
	ReachabilityVerdict m_verdictV4;
	ReachabilityVerdict m_verdictV6;

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

	QComboBox* m_mode;
	QSpinBox* m_port;
	QLineEdit* m_bind;
	QLineEdit* m_address;

	/*
	 * Whether anything outside can reach the port above, and who was asked.
	 *
	 * The server's name is the only setting of the group; everything under it is
	 * read-only, because a measurement is not a preference and a field offering to
	 * edit one would be offering to edit the network. The three sentences the
	 * measuring library writes - 'm_dropReason' carries its reason and its remedies,
	 * 'm_dropNote' its note - are shown as they arrive rather than reworded: a user
	 * who is given two different explanations of the same network by two clients
	 * concludes that one of them is broken. Every one of these labels is on
	 * Qt::PlainText for the same reason, none of them left to guess: a label asked to
	 * guess reads a sentence with an angle bracket in it as markup, and showing the
	 * words unaltered is the whole point.
	 *
	 * 'm_dropAttempt' is how the last attempt went, which is a different thing from
	 * what was measured and so has a label of its own. A check that could not be run
	 * says nothing about whether this client is reachable, so it must not overwrite an
	 * answer that was measured earlier and still stands. It is also where a check that
	 * has been asked for says so, rather than 'm_dropCheck' being disabled on the way
	 * out: a button disabled there has to be re-enabled by something coming back, and a
	 * request the bridge decides not to make - no server named yet, or one already in
	 * flight - reports nothing at all, which would leave the button dead for the life
	 * of the window.
	 */
	QLineEdit* m_dropServer;
	QLabel* m_dropAddress;
	QLabel* m_dropReachable;
	QLabel* m_dropChecked;
	QPushButton* m_dropCheck;
	QLabel* m_dropReason;
	QLabel* m_dropNote;
	QLabel* m_dropAttempt;

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
