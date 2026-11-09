"""The walkthrough's scenes: what is said and shown, in order. Built by make_walkthrough.py."""

from __future__ import annotations


def scenes(Shot, Scene):
    ride = [Shot(f"ride-{i:02d}.png") for i in range(1, 11)]
    return [
        Scene(
            title="SB20 Power Proxy",
            caption="A walkthrough for both boards: the screen board and the headless board.",
            say=(
                "This is a walkthrough of the S B twenty Power Proxy. It reads your own power meter, "
                "and presents it to your Stages S B twenty as if it were the bike's own crank. "
                "There are two boards. One has a three and a half inch touch screen. "
                "The other is a tiny headless board with a small display. "
                "Setup is nearly the same on both, so we'll go through it once."
            ),
            device="none",
        ),
        Scene(
            title="Install the firmware",
            tag="Both boards",
            caption="Download your board's image from the release page, then write it at offset 0 "
                    "with esptool.\nA data USB cable is essential.",
            say=(
                "First, install the firmware. Connect the board with a U S B cable that carries data. "
                "Download the image for your board from the release page, and write it with esptool. "
                "The exact command is in the guide. Installing erases the board's settings, "
                "so you'll only want to do this once."
            ),
            device="none",
        ),
        Scene(
            title="Join the setup WiFi",
            tag="Screen board",
            caption="Scan the QR code with your phone's camera.",
            say=(
                "After installing, the board starts its own WiFi network. "
                "On the screen board, just scan the Q R code with your phone's camera, "
                "and accept the prompt to join."
            ),
            shots=[Shot("guition-wifi-qr.png")],
        ),
        Scene(
            title="Join the setup WiFi",
            tag="Headless board",
            caption="Join the network named on the display, using the PIN as the password.",
            say=(
                "On the headless board, the tiny display shows the network's name, and a PIN. "
                "Join that network from your phone's WiFi settings, and use the PIN as the password. "
                "This picture is an illustration of what the display shows."
            ),
            shots=[Shot("oled-setup-large.png")],
            device="oled",
        ),
        Scene(
            title="Choose your home WiFi",
            tag="Both boards",
            caption="Pick your network, enter its password, and tap Save & Connect.\nThe board restarts and joins it.",
            say=(
                "A setup page opens on your phone. Pick your home network, type its password, and tap Save and Connect. "
                "The board restarts, joins your WiFi, and shows its address on its display. "
                "Now put your phone back on your home WiFi."
            ),
            shots=[Shot("portal-page.png")],
            device="phone",
        ),
        Scene(
            title="Choose your power meter",
            tag="Screen board",
            caption="Spin the pedals. Setup, Rescan, tap your meter, then Save.",
            say=(
                "Now choose your meter. Spin the pedals, so that it wakes up. "
                "On the screen board, tap Setup, then Rescan. "
                "Tap your meter in the list, and if you'll run workouts from the board, tap your trainer too. "
                "Then tap Save, and the board restarts."
            ),
            shots=[
                Shot("guition-setup-empty.png", tap=(82, 426)),
                Shot("guition-setup-pick.png", tap=(160, 71)),
                Shot("guition-setup-picked.png", tap=(238, 426)),
            ],
        ),
        Scene(
            title="Choose your power meter",
            tag="Headless board",
            caption="Open /setup on the board's address, tap Scan, and pick your meter.",
            say=(
                "On the headless board, use a web page instead. "
                "Open the address S B twenty proxy dot local, slash setup, on your phone. "
                "Tap Scan, and pick your meter."
            ),
            shots=[Shot("web-setup.png")],
            device="phone",
        ),
        Scene(
            title="Set the crank identity",
            tag="Both boards",
            caption="On /setup, open Crank identity and type your number, digits only.\nThis is the step that most often stops pairing.",
            say=(
                "Next is the step that most often stops pairing. The bike looks for its crank by name. "
                "On the setup page, open Crank identity, and type the number you want the board to use, "
                "digits only. Don't leave it blank. "
                "If your meter only reports one leg, tick Single sided as well. Then tap Save."
            ),
            shots=[Shot("web-identity.png")],
            device="phone",
        ),
        Scene(
            title="Check it before the bike",
            tag="Screen board",
            caption="Spin the pedals. Green dots mean connected, and power should follow your effort.",
            say=(
                "Before going near the bike, check that it works. Spin the pedals. "
                "On the screen board, the Ride screen shows your live power. "
                "Green dots at the top mean the meter and the crank are both up. "
                "The chart here is a simulated meter on a test bench, which is why it shows a computer's name."
            ),
            shots=ride,
        ),
        Scene(
            title="In and out, side by side",
            tag="Screen board",
            caption="Tap the top bar to compare what comes in from your meter with what goes out to the bike.",
            say=(
                "Tap the top bar, and you can see what's coming in from your meter, "
                "and what's going out to the bike, side by side."
            ),
            shots=[Shot("guition-ride.png", share=0.4, tap=(40, 10)), Shot("guition-details.png")],
        ),
        Scene(
            title="Check it before the bike",
            tag="Headless board",
            caption="The dashboard at sb20proxy.local shows the same live numbers.",
            say="On the headless board, the same live numbers are on the dashboard page.",
            shots=[Shot("web-dashboard.png")],
            device="phone",
        ),
        Scene(
            title="Pair the bike",
            tag="Both boards",
            caption="In the Stages app: left crank is your board's number, right crank is your real right crank.\nWake the right crank first.",
            say=(
                "Now pair the bike. In the Stages app, set the left crank to the number you gave the board, "
                "and the right crank to your real right crank. "
                "The right crank must be awake, or the app will say that pairing failed. "
                "The first connection often drops and reconnects a moment later, and that's normal."
            ),
            device="none",
        ),
        Scene(
            title="Workouts",
            tag="Screen board",
            caption="More, Workout, pick one, START.\nSet the bike's ride mode to External first.",
            say=(
                "On the screen board you can run a workout from the board itself. "
                "First set the bike's ride mode to External in the Stages app. "
                "Then tap More, Workout, and pick one. Tap Start. "
                "This feature is new, and it has only been tested against a simulated trainer, "
                "so be ready to stop."
            ),
            shots=[
                Shot("guition-more.png", tap=(160, 53)),
                Shot("guition-workout-picker.png", tap=(160, 72)),
                Shot("guition-workout-loaded.png", tap=(118, 255)),
                Shot("guition-workout-running.png", share=1.4),
            ],
        ),
        Scene(
            title="If something's wrong",
            tag="Both boards",
            caption="Open /report on the board's address, download it, and attach it to a GitHub issue.",
            say=(
                "If something's not right, don't try to fix it on the bike. "
                "Open slash report on the board's address, download the file, and attach it to a GitHub issue. "
                "Everything shown here is in the written guide. Enjoy the ride."
            ),
            device="none",
        ),
    ]
