var collator = new Intl.Collator(undefined, { numeric: true, sensitivity: 'base' });
console.log(typeof Intl.Collator, collator instanceof Intl.Collator);
console.log(collator.compare('registry2', 'registry10'));
console.log(collator.compare('Protocol', 'protocol'));
console.log(Intl.Collator.supportedLocalesOf(['en', 'fr']).join(','));
