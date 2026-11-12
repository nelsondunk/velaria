# Velaria privacy statement

**DRAFT, 12 November 2026. Not yet reviewed by a lawyer. Fill in the items in square brackets before publishing.**

This statement covers the Velaria software: the Mac components, the Velaria View app for Meta Quest, and the Velaria effects for DaVinci Resolve. It is published by [LEGAL ENTITY NAME], operating as Hammer & Anvil ("we").

## The short version

Velaria sends nothing to us or to anyone else. It has no account, no licence check, no analytics, no telemetry and no crash reporting. Your footage, your project and your settings stay on your Mac and your headset.

## What Velaria does on your network

Velaria streams the picture from DaVinci Resolve on your Mac to your headset, and sends your control changes from the headset back to Resolve. That traffic goes over the USB cable, or over your local network when you use Wi-Fi. It does not go to the internet.

Two things you should know about the Wi-Fi path:

1. To find your Mac, the headset app sends a discovery message on your local network.
2. The stream between the Mac and the headset is not encrypted, and the Mac does not ask a headset for a password. Another person on the same network who has the Velaria View app could connect and see the picture. Use the USB cable, or a network you trust, for confidential work.

## What Velaria stores

On your Mac, Velaria keeps a small settings file and log files in your Library folder (`~/Library/Application Support/Velaria` and `~/Library/Logs/Velaria`). On the headset, the app keeps its settings and panel layout. The logs record technical events such as connections, timings, versions, and the names of timelines, clips and effects that Velaria handles. None of this leaves your devices on its own.

## When you send us logs

If you ask for help, we may ask you to run `send-logs.sh`. It creates a zip file on your Desktop and sends nothing by itself. You decide whether to email it to us. The zip contains:

1. Velaria's logs and settings file from your Mac;
2. the last 2,000 lines of DaVinci Resolve's own log, which can include project names, clip names and file paths;
3. your macOS version, Mac type, and the versions of Velaria, Resolve and Python;
4. if your headset is plugged in, the headset's system log, which can include entries from other apps on the headset.

You can open the zip and remove anything before you send it. We use logs only to solve your problem, and we delete them when the problem is closed or after 12 months, whichever comes first.

## When you buy

Purchases are handled by Lemon Squeezy, the reseller and merchant of record. Lemon Squeezy collects your payment details; we never see your card number. Lemon Squeezy shares your name, email address, country and order details with us so that we can deliver the download, answer support requests, and keep the business records the law requires. Lemon Squeezy's privacy policy applies to the checkout.

We do not add you to a mailing list when you buy. We will email you about your order, and about an update that fixes a serious problem. [CONFIRM: any opt-in list for update news.]

## When you email us

We keep your messages so that we can answer them and follow up. We do not sell or share your information, except with the service providers that run our email and store, or where the law requires it.

## The website

The Velaria web page is separate from the software. [CONFIRM AT PUBLISH: the page as drafted loads fonts from Google Fonts and a script from cdnjs, so those providers receive visitors' IP addresses. Either host those files on the H&A site or say so here. State here whether the H&A site uses analytics or cookies.]

## Your rights

You can ask us what personal information we hold about you, ask us to correct or delete it, or raise a concern, by writing to **[SUPPORT EMAIL]**. We are based in Ontario, Canada, and follow Canada's federal privacy law (PIPEDA). If you are in the European Union or the United Kingdom, you also have the rights your local law gives you.

## Changes

If Velaria ever changes in a way that affects this statement, for example an optional update check, we will change this statement first and describe the change in the release notes.

## Contact

[LEGAL ENTITY NAME], operating as Hammer & Anvil
[MAILING ADDRESS]
[SUPPORT EMAIL]
