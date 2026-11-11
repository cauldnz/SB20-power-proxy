"""The walkthrough's scenes: what is said and shown, in order. Built by make_walkthrough.py."""

from __future__ import annotations


def scenes(Shot, Scene):
    ride = [Shot(f"ride-{i:02d}.png") for i in range(1, 11)]
    return [
        Scene(
            title="SB20 Power Proxy",
            caption="A walkthrough for both boards: the screen board and the headless board.\n"
                    "Narration is AI-generated.",
            say=(
                "This is a walkthrough of the S B twenty Power Proxy. It reads your power meter pedals, "
                "and presents them to your Stages S B twenty as if they were the bike's own crank. "
                "It is not yet tested with a Stages S B two hundred, but if we can get our hands on one, "
                "it should port across."
            ),
            device="none",
        ),
        Scene(
            title="Why you'd want this",
            caption="One power number you can trust, indoors and out.\n"
                    "The bike's erg runs on your pedals, not its cranks.",
            say=(
                "So why would you want this? If you train with power, you want one number you can trust, "
                "indoors and out. The S B twenty measures power with its own cranks, and they may not agree "
                "with the pedals on your road bike. On our test bike the difference wasn't even a fixed amount. "
                "It changed with cadence, so one correction factor can't fix it. "
                "Pairing your pedals to your training app doesn't solve it either, "
                "because the bike's erg mode still steers by its own cranks. "
                "With this board, the bike itself runs on your pedals, "
                "so two hundred and fifty watts means two hundred and fifty watts on the numbers you already train by."
            ),
            device="none",
        ),
        Scene(
            title="Your apps don't change",
            caption="It all happens between the pedals and the bike. "
                    "Your app just sees a normal Stages bike.",
            say=(
                "The important part is where this happens. It's entirely between the pedals and the bike. "
                "Your pedals talk to the board, and the board talks to the bike. "
                "Zwift, or any other trainer app, connects to the bike exactly as it always has, "
                "and just sees a normal Stages bike. "
                "There's nothing to install in the app, and nothing to change in it. "
                "Nothing inside the bike is modified either."
            ),
            device="none",
            flow=("Your pedals", "Power Proxy board", "Stages SB20", "Zwift or any app"),
        ),
        Scene(
            title="The boards",
            caption="ESP32 boards: a touch screen head unit, or a tiny headless board.\n"
                    "Your phone works as a remote screen for either.",
            say=(
                "It is designed to run on E S P thirty two microprocessor boards. "
                "Initially we target two boards, but it is easy to port to something new. "
                "One board has a three and a half inch touch screen, "
                "and can act as a full head unit for your Stages bike. "
                "The other is a tiny headless board, with a small display for diagnostics. "
                "Both boards support using your phone as a remote user interface. "
                "Setup is nearly the same on both, so we'll go through it once."
            ),
            shots=[Shot("guition-ride.png")],
        ),
        Scene(
            title="Install the firmware",
            tag="Both boards",
            caption="Open the installer in desktop Chrome or Edge, click Install for your board, "
                    "and pick its port.\nA data USB cable is essential.",
            say=(
                "First, install the firmware. Connect the board to a computer with a U S B cable that "
                "carries data. Open the installer page, linked from the guide, in desktop Chrome or Edge. "
                "Click Install for your board, and pick its port. Installing erases the board's settings, "
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
                "Now choose your pedal power meter. Spin the pedals, so that they wake up. "
                "This is getting the E S P board to listen to the pedals on Bluetooth. "
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
                "On the headless board, use the web page instead. "
                "Open the address S B twenty proxy dot local, slash setup, on your phone. "
                "Tap Scan, and pick your meter."
            ),
            shots=[Shot("web-setup.png")],
            device="phone",
        ),
        Scene(
            title="Set the crank identity",
            tag="Both boards",
            caption="The board makes up its own Stages name. You'll need its number to pair the bike.\n"
                    "To choose your own, open Crank identity on /setup and type it, digits only.",
            say=(
                "Next is the step that sometimes causes problems. The bike looks for its crank by name. "
                "The board makes up its own name automatically, the word Stages and a number, "
                "and you'll need that number to pair the bike. "
                "If you'd rather choose your own, open Crank identity on the setup page, "
                "and type the number, digits only, up to sixty five thousand five hundred and thirty five. "
                "If your meter only reports one leg, tick Single sided as well. Then tap Save."
            ),
            shots=[Shot("web-identity.png")],
            device="phone",
        ),
        Scene(
            title="Have a spin",
            tag="Screen board",
            caption="A green dot means your pedals are connected. Next to them is the Stages name you're broadcasting.\n"
                    "Shown here: a simulated meter on a test bench.",
            say=(
                "Jump on the bike and have a spin. "
                "In the U I, a green dot at the top means your pedals are connected. "
                "You'll see the name of your pedals, and the name you are broadcasting, "
                "which will look like a Stages crank name."
            ),
            shots=ride,
        ),
        Scene(
            title="In and out, side by side",
            tag="Screen board",
            caption="Tap the top bar to compare what comes in from your meter with what goes out to the bike.",
            say=(
                "Tap the top bar, and you can see what's coming in from your meter, "
                "and what's going out to the bike, side by side. "
                "For most users this is just going to be a straight pass through, "
                "but we have some use cases where you can apply an adjustment model too."
            ),
            shots=[Shot("guition-ride.png", share=0.4, tap=(40, 10)), Shot("guition-details.png")],
        ),
        Scene(
            title="Have a spin",
            tag="Headless board",
            caption="The dashboard at sb20proxy.local shows the same live numbers.",
            say="On the headless board, the same live numbers are on the dashboard page.",
            shots=[Shot("web-dashboard.png")],
            device="phone",
        ),
        Scene(
            title="Pair the bike",
            tag="Both boards",
            caption="In the Stages app: set the left crank to your board's number. Leave the right crank as it is.",
            say=(
                "Now pair the bike. In the Stages app, set the left crank to your board's number, "
                "and leave the right crank as it is. "
                "The first connection often drops and reconnects a moment later, and that's normal."
            ),
            device="none",
        ),
        Scene(
            title="Ride as normal",
            tag="Both boards",
            caption="Your pedals are now pretending to be the Stages cranks.",
            say=(
                "Now just use your bike like normal. Open the Stages app, and everything should run normally. "
                "The only difference is that instead of the Stages cranks providing the bike's power information, "
                "it's your pedals pretending to be the Stages cranks."
            ),
            device="none",
        ),
        Scene(
            title="If something's wrong",
            tag="Both boards",
            caption="Open /report on the board's address, download it, and attach it to a GitHub issue.",
            say=(
                "If something's not right, don't try to fix it on the bike. "
                "Open slash report on the board's address, download the file, and attach it to a GitHub issue. "
                "Everything shown here is in the written guide."
            ),
            device="none",
        ),
        Scene(
            title="Coming soon",
            caption="Calibration profiles: make your Stages cranks read the same as your pedals\n"
                    "OpenBikeControl: map the Stages buttons into apps, with Zwift support in the works\n"
                    "Better integration with qdomyos-zwift, for Peloton rides and more\n"
                    "Workouts run from the touch screen\n"
                    "Updates over WiFi, and a small board that adds ANT+",
            say=(
                "Finally, here's what's coming soon. First, power meter calibration. "
                "You'll build a calibration profile with your pedals. "
                "The board can then read the bike's Stages cranks, "
                "and we can build a regression model to match them to your pedals. "
                "You can then proxy your cranks, and just leave your pedals on your road bike. "
                "We make your cranks read the same as your pedals across all torque values, "
                "that is, a full range of cadence, and force and power levels. "
                "That keeps your Garmin training load and training status consistent. "
                "Next, Open Bike Control, so the Stages buttons can be mapped into apps that support it, "
                "like My Whoosh. We are also working on support for using the buttons in Zwift. "
                "Then better integration with Q Domyos Zwift, for Peloton rides and other advanced things. "
                "And workouts run from the touch screen, updates over WiFi with no cable, "
                "and a small board that adds Ant plus. "
                "Enjoy the ride."
            ),
            device="none",
        ),
    ]
