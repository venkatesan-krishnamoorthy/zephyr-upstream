.. zephyr:code-sample:: ti_am13_escss
   :name: TI AM13 EtherCAT subsystem
   :relevant-api: ti_am13_escss_interface

   Bring up the AM13 EtherCAT subsystem and report ESC events.

Overview
********

This sample brings up the EtherCAT Subsystem (ESCSS) of a TI AM13E232x
device and prints the ESCSS IP revision and the Beckhoff ESC type and
revision. It then enables the PDI and SYNC0 events and prints the ESC AL
status and the event counts once per second.

Requirements
************

- An AM13E232x board with the ``escss`` devicetree node enabled, the
  EtherCAT pins muxed through ``pinctrl-0`` and an SII EEPROM connected to
  the ESC.
- An EtherCAT master, for example TwinCAT, to see the AL state change and
  SYNC0 events.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/misc/ti_am13_escss
   :board: <am13e232x board>
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

   ESCSS IP revision 0x10
   ESC type 0xc0 revision 0x01 build 0x0000
   AL status 0x0001, PDI events 0, SYNC0 events 0
