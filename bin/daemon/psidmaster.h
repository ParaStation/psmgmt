/*
 *               ParaStation
 * psidmaster.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidmaster.h,v 1.1 2003/10/09 19:20:39 eicker Exp $
 *
 */
/**
 * @file
 * Helper functions for master-node detection and actions.
 *
 * $Id: psidmaster.h,v 1.1 2003/10/09 19:20:39 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDMASTER_H
#define __PSIDMASTER_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

void setMasterNode(unsigned short id);

unsigned short getMasterNode(void);

int amMasterNode(void);

/*
 * Vorgehen:
 *
 * Falls eine Nachfrage nach einer Partition kommt:
 *  - Knoten ist nicht master -> zum Master weiterleiten. Ende.
 *       Kommt mit Fehler zurück: Erneut behandeln (Master tot?).
 *  - Knoten ist master -> speichern.
 *
 * Weitere Punkte nur auf Master:
 *
 * Abfrage vollständig empfangen:
 *  - Nachfrage ist erste Nachfrage -> gatherMasterInfo(). Ende.
 *  - Cluster Information noch nicht vollständig -> Ende.
 *  - Cluster Information bereits vollständig -> 
 *
 * Test, ob alle outstanding answers (siehe gatherMasterInfo) da sind
 * Immer in declareNodeDead() und msg_PROVIDETASKS().
 * Sind alle Antworten da -> Arbeite Liste der vollständigen Abfragen ab.
 *
 * Außerdem:
 *   Lasse Nachrichten schicken, wenn eine Task beendet ist.
 *   Falls Knoten runtergeht: gib Tasks auf diesem Knoten frei.
 *
 *   Immer wenn Task freigegeben wird: Versuche wartende Tasks zu starten.
 *
 * Falls sich ein Knoten verbindet, während auf Antworten gewartet
 * wird (und auf notSend steht), schaue in DAEMONCONNECT Nachricht
 * nach Anzahl der Tasks. Evtl. Tasks schicken lassen.
 *
 * Falls neuer Knoten kommt, der nun Master wird:
 *  - Gesammte lokale Info wegwerfen und das auch markieren (alle auf notSent)
 *      (allerdings nur, falls Info schon nachgefragt/empfangen wurde (flag))
 *  - Neuer Master eintragen.
 *  - Offene lokale Nachfragen zum neuen Master schicken.
 *
 * Flags auf master:
 *   - taskRequested (lokale Info angefragt. Evtl. noch Antworten offen)
 *   - outstandingAnswers (Zahl der offenen GETTASKS Nachrichten)
 *
 * Falls nicht Master:
 *   - taskRequested = 0
 *
 * Alle Knoten:
 *
 * Erweiterung: Nachfragen zwischenspeichern, falls Master wechselt
 * während auf Nachfrage gewartet wird. (request in Task!)
 *
 * Wenn Master wechselt:
 *  - Teilweise empfangene Partitionen (vom alten Master) wegwerfen.
 *  - Alle offenen Nachfragen zum neuen Master senden.
 *
 * Grundsätzlich:
 *  - Partitionen nur vom aktuellen Master empfangen.
 *  - Falls Partition vollständig ist: Nachfrage löschen.
 *
 */

/**
 * Sende PSP_DD_GETTASKS Nachricht zu allen Knoten, die "oben" sind.
 * Setze Flags bezüglich outstanding answers (Sent, received, notSent).
 *
 * Löschen dieser Flags, wenn Finale PSP_DD_PROVIDETASKS Nachricht von
 * diesem Knoten kommt, oder wenn dieser Knoten DOWN
 * (declareNodeDead()) geht (outstandingAnswers reduzieren). (asynchron!!)
 *
 * Antworten *nur* annehmen, falls taskRequested.
 * 
 *
 */
gatherMasterInfo();


/**
 * @brief Handle a PSP_DD_GETTASKS message.
 *
 * Handle the message @a inmsg of type PSP_DD_GETTASKS.
 *
 * This function will send one or more PSP_DD_PROVIDETASKS for each
 * parallel tasks whose root process resides on this node. Each
 * message contains a list of node IDs with up to @ref GETNODES_CHUNK
 * IDs. If a tasks has allocated more than this number of nodes, more
 * messages are sent.
 *
 * Finally a message
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_GETTASKS(DDBufferMsg_t *inmsg);

/**
 * @brief Handle a PSP_DD_PROVIDEJOBS message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETNODES.
 *
 *
 * Antworten *nur* annehmen, falls taskRequested.
 *
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_PROVIDETASKS(DDBufferMsg_t *inmsg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDMASTER_H */
