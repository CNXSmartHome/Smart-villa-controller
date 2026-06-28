# SVC-100 Schematic Checklist Rev A

## Global

- [ ] ERC clean
- [ ] All power rails labelled
- [ ] All connectors labelled
- [ ] All test points labelled
- [ ] All DNP parts marked
- [ ] All GPIOs match pin database
- [ ] All safety-critical nets reviewed

## Power

- [ ] Fuse/PTC selected
- [ ] Reverse polarity protection selected
- [ ] TVS selected
- [ ] 5V buck selected
- [ ] 3.3V regulator selected
- [ ] Bulk capacitance sized
- [ ] ESP32 power margin checked

## ESP32

- [ ] EN/reset correct
- [ ] BOOT button correct
- [ ] USB correct
- [ ] Strapping pins reviewed
- [ ] Antenna keepout documented

## RS485

- [ ] TVS on A/B
- [ ] Termination jumper
- [ ] Bias option
- [ ] Sensor power fuse
- [ ] A/B labels clear

## Relay

- [ ] Driver protection
- [ ] Safe-off boot
- [ ] COM/NO/NC labels
- [ ] Clearance reviewed

## Digital Inputs

- [ ] Input protection
- [ ] Pull-up/down
- [ ] Debounce filter footprint
- [ ] Dry contact wiring clear

## Release Gate

- [ ] CTO review
- [ ] Codex review
- [ ] Hardware engineer review
