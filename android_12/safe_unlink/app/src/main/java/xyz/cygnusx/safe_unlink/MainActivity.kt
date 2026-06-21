package xyz.cygnusx.safe_unlink

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.widget.TextView
import xyz.cygnusx.safe_unlink.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        run()
    }

    /**
     * A native method that is implemented by the 'safe_unlink' native library,
     * which is packaged with this application.
     */
    external fun run()

    companion object {
        // Used to load the 'safe_unlink' library on application startup.
        init {
            System.loadLibrary("safe_unlink")
        }
    }
}