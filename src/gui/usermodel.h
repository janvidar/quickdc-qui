/*
 * Copyright (C) 2026 Jan Vidar Krey, janvidar@extatic.org
 */

#ifndef HAVE_QUICKDC_QUI_USERMODEL_H
#define HAVE_QUICKDC_QUI_USERMODEL_H

#include <core/coretypes.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>

namespace Qdc {

/**
 * Who is on a hub.
 *
 * Kept from the hub bridge's join, update and leave events rather than fetched,
 * because the core has nothing to fetch it from: UserManager walks its users with
 * a single shared iterator, which is no basis for a model, and the pointers it
 * hands out are freed the moment a user leaves.
 *
 * Ordered with operators first and then by nickname, which is what other clients
 * do. Kept sorted on insert rather than behind a proxy model: the comparison is
 * fixed, and a proxy would only add a layer of indices to translate.
 */
class UserModel final : public QAbstractListModel {
	Q_OBJECT

public:
	enum Role {
		UserRole = Qt::UserRole + 1
	};

	explicit UserModel(QObject* parent = nullptr);

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role) const override;

	/**
	 * A user has arrived, or their details changed. Either way this is the whole
	 * of what is known about them, so an existing row is replaced rather than
	 * merged - the core sends a complete record each time.
	 */
	void upsert(const UserInfo& user);

	/**
	 * A nickname with nothing else known yet, from a join.
	 *
	 * The core raises a join and an info event for the same new user, and the join
	 * carries only a nickname while the info carries the id. On NMDC those are the
	 * same string and the row is simply filled in; on ADC the id is a session id,
	 * so the row this adds is a placeholder that upsert() replaces once the real
	 * record arrives. Without that replacement every ADC user appears twice.
	 */
	void noteNickname(const QString& nick);

	/** By the id the core knows them by; a no-op if they are not listed. */
	void remove(const QString& id);

	void clear();
	int count() const;

	/** An empty record if there is no such row. */
	UserInfo at(int row) const;

	/**
	 * By the id the core knows them by. An empty record if nobody here has it.
	 *
	 * For naming a conversation opened by a message rather than by picking
	 * someone out of the list, where all that arrives is the id.
	 */
	UserInfo byId(const QString& id) const;

	/** For the composer's mention completion, in displayed order. */
	QStringList nicknames() const;

signals:
	/** Any change to the number of rows, for the header's user count. */
	void countChanged(int count);

private:
	static bool precedes(const UserInfo& left, const UserInfo& right);

	/** @return the row 'user' should be inserted at to keep the order. */
	int placeFor(const UserInfo& user) const;

	/** Rebuild the id index from 'from' onwards, after rows have shifted. */
	void reindex(int from);

	QList<UserInfo> m_users;

	/** Row by user id, so an update or a departure is not a linear scan. */
	QHash<QString, int> m_rows;

	/**
	 * Ids of rows that came from a bare nickname and are standing in for a user
	 * whose real record has not arrived.
	 *
	 * Tracked rather than inferred from an empty field, because a real user may
	 * legitimately have no description or share and would then look like a
	 * placeholder.
	 */
	QSet<QString> m_placeholders;
};

}

#endif // HAVE_QUICKDC_QUI_USERMODEL_H
