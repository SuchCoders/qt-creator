/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "sqlitedatabase.h"

#include "sqlitetable.h"
#include "sqlitetransaction.h"
#include "sqlitereadwritestatement.h"

#include <chrono>

using namespace std::chrono_literals;

namespace Sqlite {

Database::Database()
    : m_databaseBackend(*this)
{
}

Database::Database(Utils::PathString &&databaseFilePath, JournalMode journalMode)
    : Database(std::move(databaseFilePath), 0ms, journalMode)
{
}

Database::Database(Utils::PathString &&databaseFilePath,
                   std::chrono::milliseconds busyTimeout,
                   JournalMode journalMode)
    : m_databaseBackend(*this),
      m_busyTimeout(busyTimeout)
{
    setJournalMode(journalMode);
    open(std::move(databaseFilePath));
}

Database::~Database() = default;

void Database::open()
{
    m_databaseBackend.open(m_databaseFilePath, m_openMode);
    m_databaseBackend.setJournalMode(m_journalMode);
    m_databaseBackend.setBusyTimeout(m_busyTimeout);
    registerTransactionStatements();
    initializeTables();
    m_isOpen = true;
}

void Database::open(Utils::PathString &&databaseFilePath)
{
    setDatabaseFilePath(std::move(databaseFilePath));
    open();
}

void Database::close()
{
    m_isOpen = false;
    m_databaseBackend.close();
}

bool Database::isOpen() const
{
    return m_isOpen;
}

Table &Database::addTable()
{
    m_sqliteTables.emplace_back();

    return m_sqliteTables.back();
}

const std::vector<Table> &Database::tables() const
{
    return m_sqliteTables;
}

void Database::setDatabaseFilePath(Utils::PathString &&databaseFilePath)
{
    m_databaseFilePath = std::move(databaseFilePath);
}

const Utils::PathString &Database::databaseFilePath() const
{
    return m_databaseFilePath;
}

void Database::setJournalMode(JournalMode journalMode)
{
    m_journalMode = journalMode;
}

JournalMode Database::journalMode() const
{
    return m_journalMode;
}

void Database::setOpenMode(OpenMode openMode)
{
    m_openMode = openMode;
}

OpenMode Database::openMode() const
{
    return m_openMode;
}

void Database::execute(Utils::SmallStringView sqlStatement)
{
    m_databaseBackend.execute(sqlStatement);
}

void Database::initializeTables()
{
    try {
        ImmediateTransaction transaction(*this);

        for (Table &table : m_sqliteTables)
            table.initialize(*this);

        transaction.commit();
    } catch (const StatementIsBusy &) {
        initializeTables();
    }
}

void Database::registerTransactionStatements()
{
    m_deferredBeginStatement = std::make_unique<ReadWriteStatement>("BEGIN", *this);
    m_immediateBeginStatement = std::make_unique<ReadWriteStatement>("BEGIN IMMEDIATE", *this);
    m_exclusiveBeginStatement = std::make_unique<ReadWriteStatement>("BEGIN EXCLUSIVE", *this);
    m_commitBeginStatement = std::make_unique<ReadWriteStatement>("COMMIT", *this);
    m_rollbackBeginStatement = std::make_unique<ReadWriteStatement>("ROLLBACK", *this);
}

void Database::deferredBegin()
{
    m_databaseMutex.lock();
    m_deferredBeginStatement->execute();
}

void Database::immediateBegin()
{
    m_databaseMutex.lock();
    m_immediateBeginStatement->execute();
}

void Database::exclusiveBegin()
{
    m_databaseMutex.lock();
    m_exclusiveBeginStatement->execute();
}

void Database::commit()
{
    m_commitBeginStatement->execute();
    m_databaseMutex.unlock();
}

void Database::rollback()
{
    m_rollbackBeginStatement->execute();
    m_databaseMutex.unlock();
}

DatabaseBackend &Database::backend()
{
    return m_databaseBackend;
}

} // namespace Sqlite
